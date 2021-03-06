#include <DB/IO/WriteHelpers.h>
#include <DB/IO/WriteBufferValidUTF8.h>
#include <DB/DataStreams/JSONRowOutputStream.h>


namespace DB
{


JSONRowOutputStream::JSONRowOutputStream(WriteBuffer & ostr_, const Block & sample_, bool write_statistics_, bool force_quoting_64bit_integers_)
	: dst_ostr(ostr_), write_statistics(write_statistics_), force_quoting_64bit_integers(force_quoting_64bit_integers_)
{
	NamesAndTypesList columns(sample_.getColumnsList());
	fields.assign(columns.begin(), columns.end());

	bool have_non_numeric_columns = false;
	for (size_t i = 0; i < sample_.columns(); ++i)
	{
		if (!sample_.unsafeGetByPosition(i).type->isNumeric())
			have_non_numeric_columns = true;

		String field_name_quoted;
		{
			WriteBufferFromString out(field_name_quoted);
			writeJSONString(fields[i].name, out);
		}
		fields[i].name = field_name_quoted;
	}

	if (have_non_numeric_columns)
	{
		validating_ostr = std::make_unique<WriteBufferValidUTF8>(dst_ostr);
		ostr = validating_ostr.get();
	}
	else
		ostr = &dst_ostr;
}


void JSONRowOutputStream::writePrefix()
{
	writeCString("{\n", *ostr);
	writeCString("\t\"meta\":\n", *ostr);
	writeCString("\t[\n", *ostr);

	for (size_t i = 0; i < fields.size(); ++i)
	{
		writeCString("\t\t{\n", *ostr);

		writeCString("\t\t\t\"name\": ", *ostr);
		writeString(fields[i].name, *ostr);
		writeCString(",\n", *ostr);
		writeCString("\t\t\t\"type\": ", *ostr);
		writeJSONString(fields[i].type->getName(), *ostr);
		writeChar('\n', *ostr);

		writeCString("\t\t}", *ostr);
		if (i + 1 < fields.size())
			writeChar(',', *ostr);
		writeChar('\n', *ostr);
	}

	writeCString("\t],\n", *ostr);
	writeChar('\n', *ostr);
	writeCString("\t\"data\":\n", *ostr);
	writeCString("\t[\n", *ostr);
}


void JSONRowOutputStream::writeField(const IColumn & column, const IDataType & type, size_t row_num)
{
	writeCString("\t\t\t", *ostr);
	writeString(fields[field_number].name, *ostr);
	writeCString(": ", *ostr);
	type.serializeTextJSON(column, row_num, *ostr, force_quoting_64bit_integers);
	++field_number;
}


void JSONRowOutputStream::writeFieldDelimiter()
{
	writeCString(",\n", *ostr);
}


void JSONRowOutputStream::writeRowStartDelimiter()
{
	if (row_count > 0)
		writeCString(",\n", *ostr);
	writeCString("\t\t{\n", *ostr);
}


void JSONRowOutputStream::writeRowEndDelimiter()
{
	writeChar('\n', *ostr);
	writeCString("\t\t}", *ostr);
	field_number = 0;
	++row_count;
}


void JSONRowOutputStream::writeSuffix()
{
	writeChar('\n', *ostr);
	writeCString("\t]", *ostr);

	writeTotals();
	writeExtremes();

	writeCString(",\n\n", *ostr);
	writeCString("\t\"rows\": ", *ostr);
	writeIntText(row_count, *ostr);

	writeRowsBeforeLimitAtLeast();

	if (write_statistics)
		writeStatistics();

	writeChar('\n', *ostr);
	writeCString("}\n", *ostr);
	ostr->next();
}

void JSONRowOutputStream::writeRowsBeforeLimitAtLeast()
{
	if (applied_limit)
	{
		writeCString(",\n\n", *ostr);
		writeCString("\t\"rows_before_limit_at_least\": ", *ostr);
		writeIntText(rows_before_limit, *ostr);
	}
}

void JSONRowOutputStream::writeTotals()
{
	if (totals)
	{
		writeCString(",\n", *ostr);
		writeChar('\n', *ostr);
		writeCString("\t\"totals\":\n", *ostr);
		writeCString("\t{\n", *ostr);

		size_t totals_columns = totals.columns();
		for (size_t i = 0; i < totals_columns; ++i)
		{
			const ColumnWithTypeAndName & column = totals.getByPosition(i);

			if (i != 0)
				writeCString(",\n", *ostr);

			writeCString("\t\t", *ostr);
			writeJSONString(column.name, *ostr);
			writeCString(": ", *ostr);
			column.type->serializeTextJSON(*column.column.get(), 0, *ostr, force_quoting_64bit_integers);
		}

		writeChar('\n', *ostr);
		writeCString("\t}", *ostr);
	}
}


static void writeExtremesElement(const char * title, const Block & extremes, size_t row_num, WriteBuffer & ostr, bool force_quoting_64bit_integers)
{
	writeCString("\t\t\"", ostr);
	writeCString(title, ostr);
	writeCString("\":\n", ostr);
	writeCString("\t\t{\n", ostr);

	size_t extremes_columns = extremes.columns();
	for (size_t i = 0; i < extremes_columns; ++i)
	{
		const ColumnWithTypeAndName & column = extremes.getByPosition(i);

		if (i != 0)
			writeCString(",\n", ostr);

		writeCString("\t\t\t", ostr);
		writeJSONString(column.name, ostr);
		writeCString(": ", ostr);
		column.type->serializeTextJSON(*column.column.get(), row_num, ostr, force_quoting_64bit_integers);
	}

	writeChar('\n', ostr);
	writeCString("\t\t}", ostr);
}

void JSONRowOutputStream::writeExtremes()
{
	if (extremes)
	{
		writeCString(",\n", *ostr);
		writeChar('\n', *ostr);
		writeCString("\t\"extremes\":\n", *ostr);
		writeCString("\t{\n", *ostr);

		writeExtremesElement("min", extremes, 0, *ostr, force_quoting_64bit_integers);
		writeCString(",\n", *ostr);
		writeExtremesElement("max", extremes, 1, *ostr, force_quoting_64bit_integers);

		writeChar('\n', *ostr);
		writeCString("\t}", *ostr);
	}
}


void JSONRowOutputStream::onProgress(const Progress & value)
{
	progress.incrementPiecewiseAtomically(value);
}


void JSONRowOutputStream::writeStatistics()
{
	writeCString(",\n\n", *ostr);
	writeCString("\t\"statistics\":\n", *ostr);
	writeCString("\t{\n", *ostr);

	writeCString("\t\t\"elapsed\": ", *ostr);
	writeText(watch.elapsedSeconds(), *ostr);
	writeCString(",\n", *ostr);
	writeCString("\t\t\"rows_read\": ", *ostr);
	writeText(progress.rows.load(), *ostr);
	writeCString(",\n", *ostr);
	writeCString("\t\t\"bytes_read\": ", *ostr);
	writeText(progress.bytes.load(), *ostr);
	writeChar('\n', *ostr);

	writeCString("\t}", *ostr);
}

}
