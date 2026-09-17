# Acceptance self-test fixture (H02): a child that hangs well past any case deadline.
# The runner must time out, kill ONLY this child's process tree, and keep running.
Write-Output "acceptance-selftest: sleeping 60s to force a deadline timeout"
Start-Sleep -Seconds 60
exit 0
