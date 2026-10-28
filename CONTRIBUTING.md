# Contributing

Thanks for looking. Keep changes small and measured.

## Build and test

```sh
make                 # C11, gcc or clang, no dependencies
make test            # perft quick tier + bench signature + UCI tests (pip install chess)
./tests/perft_suite.sh ./cbitboard full   # deep perft tier, ~1 min
```

CI builds with `-Wall -Wextra -Werror` on gcc and clang and runs ASan/UBSan, so warnings are errors.

## Rules

- **Move generation changes** must keep every row of `tests/perft.tsv` passing.
- **Search/eval changes** alter the deterministic bench node count. Update `tests/bench_nodes.txt`
  (`./cbitboard bench | grep signature`) in the same commit and say why in the message.
- **Strength claims** need a test: use `make sprt BASE=<old-binary>` (needs [fastchess](https://github.com/Disservin/fastchess)).
  State the settings (fixed nodes vs time control), game count and LLR bounds. Don't report an SPRT as a rating.
- **Speed claims** need real before/after numbers from your machine; mention load and how you reduced noise.
- Don't commit binaries, archives or generated output (`sprt-out/` is ignored).

## Pull requests

One topic per PR, with an honest description: what changed, what you measured, what you did not.
