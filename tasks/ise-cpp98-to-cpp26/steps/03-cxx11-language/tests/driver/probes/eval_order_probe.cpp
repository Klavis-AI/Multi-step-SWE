// A synthetic file with exactly one hazard, used to check the auditor itself.
DRV_CASE(probe)
{
    // CLEAN 1: literal contains "remove(" and a paren, but nothing mutates.
    outf("remove(x) count = %d", list.getCount());
    // CLEAN 2: one mutator, nothing else reads state.
    outf("remove -> %d", list.remove(tok(1)));
    // CLEAN 3: mutation sequenced into its own statement first.
    int n = list.remove(tok(2));
    outf("remove -> %d, now [%s]", n, dumpList(list).c_str());
    // HAZARD: mutator and a read of the same object in one argument list.
    outf("remove -> %d, now [%s]", list.remove(tok(3)), dumpList(list).c_str());
    // CLEAN 4: commented-out hazard.
    // outf("remove -> %d, now [%s]", list.remove(tok(4)), dumpList(list).c_str());
}
