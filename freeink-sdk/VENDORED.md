# Vendored FreeInk SDK

DeckPoint vendors the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) as plain
source instead of a submodule, so the SDK and reader can change together in one commit.

- **Base commit:** `bbd528ceb136696e009305377bba5bb1b079c1cf` (Merge PR #133, feat/content-rights-split)
- **License:** MIT (see `LICENSE`, `NOTICE`)
- **Upstream remote:** `upstream-sdk` (`git fetch upstream-sdk`, then diff/cherry-pick by hand)
- **Not vendored:** the `libs/assets/Icons/lucide` submodule. It is only an input to the icon
  generator; generated icons are already in the tree. To regenerate icons, clone
  https://github.com/lucide-icons/lucide into that path first.

DeckPoint changes to SDK files are marked `// DECKPOINT:`.
