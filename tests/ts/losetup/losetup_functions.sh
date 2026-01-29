
function lo_print {
	local lo=$1
	echo "offset:    $( $TS_CMD_LOSETUP --list --raw --noheadings --output OFFSET $lo )"
	echo "sizelimit: $( $TS_CMD_LOSETUP --list --raw --noheadings --output SIZELIMIT $lo )"
	echo "size:      $( $TS_CMD_LSBLK --output SIZE --bytes --noheadings --raw $lo )"
}
