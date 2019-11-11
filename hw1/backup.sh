#!/bin/bash

SOURCE_DIR=
TARGET_DIR=
VERBOSE=

while [[ "$#" > 0 ]]; do case $1 in
    --help)
        cat << EOF
Warning: very stupid argument parser is used!

--source-dir <dir>       Which dir we want to backup
--target-dir <dir>       Dir with actual backup
-v/--verbose             Print stuff
EOF
        exit 0;;
    --source-dir)
        SOURCE_DIR="$2"
        shift
        shift;;
    --target-dir)
        TARGET_DIR="$2"
        shift
        shift;;
    -v|--verbose)
        VERBOSE=-v
        shift;;
    *)
        echo "Invalid param $1, see usage: $0 --help"
        exit 1;;
esac; done

if [ "" = "$SOURCE_DIR" ]; then
    SOURCE_DIR="$HOME"
fi

if [ "" = "$TARGET_DIR" ]; then
    export TMPDIR="/tmp/ramdisk"

    PRE_DIR="`mktemp -d`"
    TARGET_DIR="$PRE_DIR/backup"
fi

say() {
    if [ "" != "$VERBOSE" ]; then
        echo "$@"
    fi
}

say "backup to $TARGET_DIR"

ORIG_PWD="$PWD"

cd "$SOURCE_DIR"

find '(' -name '*.c' -or -name '*.cpp' -or -name '*.h' -or -name '*.hpp' ')' -exec \
    sh -c 'mkdir -p "`dirname '"$TARGET_DIR/{}"'`" && cp '"$VERBOSE"' {} "'"$TARGET_DIR/{}"'"' ';'

cd "$ORIG_PWD"

say "compressing and encrypting. password is 123"

tar "$VERBOSE" -czf - "$TARGET_DIR" | openssl enc -aes-256-cbc -iter 1000 -salt -in - -out backup.tar.gz.aes -pass pass:123

if [ "$?" -eq 0 ]; then
    echo done
else
    echo error
fi
