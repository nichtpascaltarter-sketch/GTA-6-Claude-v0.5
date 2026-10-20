# The PC worker's package queue

Work packages from the cloud lead session to the user's PC, one file each: `queue/<id>.md`. A package that the lead
took back is marked with `queue/<id>.cancelled`. Results go on the `pc/results` branch. The protocol is in
`docs/PC_WORKER.md` on `main`.
