extern int cprintf(const char*,...);
int main(void){
  long a = 123456L, b = 789L;
  cprintf("hello %s: %d and %ld\n", "world", 42, a*b);
  cprintf("hex=%x width=[%5d][%-5d] char=%c\n", 0xBEEF, 42, 42, 'Z');
  return 0;
}
