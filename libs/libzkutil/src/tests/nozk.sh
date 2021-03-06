#!/bin/bash

# Добавляет в файрвол правила, не пропускающие пакеты до серверов ZooKeeper.
# Используется для тестирования поведения программ при потере соединения с ZooKeeper.
# yeszk.sh производит обратные изменения.

# Чтобы посмотреть, какие правила сейчас есть, используйте sudo iptables -L и sudo ip6tables -L

sudo iptables -A OUTPUT -p tcp --dport 2181 -j DROP
sudo ip6tables -A OUTPUT -p tcp --dport 2181 -j DROP
