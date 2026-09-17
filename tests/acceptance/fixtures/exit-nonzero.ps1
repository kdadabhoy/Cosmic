# Acceptance self-test fixture (H01): an intentional nonzero exit.
# The runner must classify this as FAILED (not a graceful pass).
Write-Output "acceptance-selftest: exiting nonzero on purpose"
Write-Error  "acceptance-selftest: simulated failure detail on stderr"
exit 7
