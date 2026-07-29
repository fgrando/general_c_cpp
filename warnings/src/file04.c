static int only_used_here(int x) { return x + 1; }
static int never_called(void)    { return 0; }   /* -Wunused-function */
int f04(int y)
{
    return only_used_here(y);
}
