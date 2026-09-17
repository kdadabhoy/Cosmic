# Acceptance self-test fixture (H01): a crashing child.
# FailFast terminates the process with a crash-class exit code (and lets Windows Error
# Reporting produce a dump), which the runner must classify as FAILED with crash=true.
Write-Output "acceptance-selftest: about to FailFast (simulated crash)"
[System.Environment]::FailFast("acceptance-selftest simulated crash")
# unreachable
exit 0
