int f07(int c)
{
    int v;
    if (c) {
        v = 10;
    }
    return v;              /* -Wmaybe-uninitialized (needs -O1+) */
}
