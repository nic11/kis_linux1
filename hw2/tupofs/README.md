Диск есть файл размером N МБ

Неделимая единица информации у меня - сектор размером 2 КБ.

Для простоты реализации блок и сектор отождествляются.

Протестировано на Ubuntu 18.04.3 LTS 64-bit с актуальными на декабрь пакетами.

## Пример конфигурации
`inode_map_size = block_map_size = 500`

Так имеем `8 * 500 = 4000` блоков. Итого ~8 МБ данных. Не густо, но сойдет

`inode_map_size = block_map_size = 1000`

`8 * 1000 = 8000` блоков. Примерно 16 МБ данных

Максимальная конфигурация - `inode_map_size = block_map_size = 2048`.
В данном случае имеем `2**3 * 2**11 = 2**14 = 16K` блоков = 32 МБ данных

## Поблочная структура
- 1 блок - суперблок
- 1 блок - i-node map
- 1 блок - block (data) map
- `8 * inode_map_size` блоков - сами i-ноды
- `8 * block_map_size` блоков - файловые данные

Итого `3 + 8 * (inode_map_size + block_map_size)` блоков в ФС =
`6 + 16 * (sum_map_size)` КБ. Это суммарный размер всей ФС

## Суперблок
Суперблок расположен с первого же байта первым сектором.

Следующая структура:
- 4 байта (magic): 00 13 37 00
- 6 байт ASCII: TupoFS
- 6 байт: 00 00 00 00 00 00
- 4 байта - `inode_map_size` - размер битмапа i-нод в байтах
- 4 байта - `block_map_size` - размер битмапа блоков в байтах

Итого 24 байта. Остальное место для простоты реализации не задействовано.
Сами битмапы расположены следующими блоками.

## Блок-битмапа
Второй и третий блок (с 1). Содержат битмапы, обозначающие факт свободности/занятости

## i-node
структура, содержащая адреса дисковых блоков с данными.
Занимает весь блок целиком.

- 1 байт - enum {DIR, FILE}
- padding
- 4 байта - индекс
- union {Dir, File}

Индекс внутрий самой i-ноды нужен для упрощения кода, можно заюзать для проверки целостности

### Dir i-node
Записи по 32 байта - инфа о дочерней папке (мб еще стоит включить . и ..)

### File i-node
Размер файла в байтах, далее:

null-terminated??? intmax-terminated??? массив из адресов (4 байта) блоков с данными

Можно указать на `2048/4 = 512` блоков. Таким образом максимальный размер файла 1 МБ

## block
Кусок данных размером с сектор (т.е. 2 КБ)