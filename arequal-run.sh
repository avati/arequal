#!/bin/bash



function do_copy()
{
    local src="$1";
    local dst="$2";

    rsync -avz $src $dst;
}


function calc_checksum()
{
    local dir="$1";

    echo "Calculating checksum on directory $dir ..."
    arequal-checksum "$dir";
    echo "-------------------------------------"
    echo
}


function main()
{
    local src="$1";
    local dst="$2";

    if [ $# -ne 2 ]; then
	echo "Usage: $0 <src> <dst>";
	echo "  e.g: $0 /usr /mnt/glusterfs/usr";
    fi

    do_copy "$src" "$dst";

    echo "Calculating checksums on source and destination";
    echo "===============================================";

    calc_checksum "$src";

    calc_checksum "$dst";
}

main "$@"
