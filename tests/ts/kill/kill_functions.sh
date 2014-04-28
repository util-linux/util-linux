
# The test_sigreceive is ready when signal process mask contains SIGHUP
function check_test_sigreceive {
	local rc=0
	local pid=$1

	for i in 0.01 0.1 1 1 1 1; do
		awk 'BEGIN { retval=1 }
		/^SigCgt/ {
			lbyte = strtonum("0x" substr($2, 16, 16))
			if (and(lbyte, 1)) {
				retval=0
			}
		} END {
			exit retval
		}' /proc/$pid/status &&
			rc=1 &&
			break
		sleep $i
	done
	return $rc
}	
