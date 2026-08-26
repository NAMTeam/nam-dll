# Tunnel portal selector resources

`NAM_TunnelPortalStyles.dat` contains NAM-owned copies of the Maxis bridge
selector UI scripts:

- `00000000-96A006B0-4A7B6E41`: tunnel portal style selector
- `00000000-96A006B0-4A7B6E42`: repeated tunnel portal style entry

The source resources are
`00000000-96A006B0-EBD0D36C` and
`00000000-96A006B0-EBD0D36D` in `SimCity_1.dat`. The copies have tunnel-style
captions and make the entry caption visible, but retain the Maxis layout,
control IDs, button sounds, and scrolling behavior.

This DAT must be installed alongside `NAM.dll`.

`NAM_TunnelPortalStyleExamples.dat` is a proof-of-concept style pack. It
contains eight selectable styles:

- Avenue: Maxis, Rivit Concrete, Maxis Ground Highway facade, and Maxis
  Raised Highway facade
- Road: Maxis and Rivit Concrete
- Rail: Maxis and Rivit Concrete

Every style is isolated under NAM-owned IDs. The package contains re-keyed
copies of all 20 zoom/rotation S3D and FSH resources for each portal model,
and every S3D material reference points at the corresponding re-keyed FSH.
The portal exemplars and SC4PATHS resources are also cloned, so installed
Maxis-ID overrides cannot change the Maxis choices and the example styles do
not override the original Maxis or Rivit resources.

The Rivit Concrete models and textures came from Rivit's Tunnel Mod. They are
included here for proof-of-concept testing and remain attributed to Rivit.

Selector icons use the native SC4 toggle-button layout: four equal-width
horizontal state frames. The example icons repeat the same artwork in four
89x58 frames, producing one 356x58 PNG.

`example-avenue-style.txt` is a canonical text-exemplar template. Its
instance and exemplar/icon IDs are placeholders and must be replaced before
packaging. See `docs/tunnel-portal-styles.md` for the property contract.

`NAM_TunnelPortalStyleRivitExtra.dat` extends Rivit Concrete coverage to the
networks the example pack does not include. It adds four selectable styles:

- OneWayRoad (1-tile, network 10)
- Ground Highway (2-tile, network 12)
- Raised Highway (2-tile, network 2)
- Rail Single Track (1-tile, network 1) — the STR concrete face on the rail
  portal, paired with a custom centered single-track path built from the STR
  path's class-1 strokes.

Like the examples, every style is fully isolated: the 20/40 zoom-rotation S3D
and FSH resources per portal are copied to NAM-owned instances (standard
`0xBADB57F1`/`0x1ABE787D`/`0xA966883F` groups), each S3D material id is rekeyed
to its copied FSH, and the native portal exemplars are cloned with
`ResourceKeyType1` repointed at the copied models. Installing (or removing)
Rivit's original replacement mod therefore cannot change or break these styles.
Selector icons (`rivit-oneway`, `rivit-ground-highway`, `rivit-raised-highway`,
`rivit-rail-str`) match the existing four-frame layout.

This DAT is produced by `tools/tunnel-style-gen` from Rivit's Tunnel Mod plus
the game's `SimCity_1.dat`; see that folder's README to regenerate it. The Rivit
Concrete models and textures remain attributed to Rivit.

`NAM_TunnelPortalStyleRivit45.dat` is an experimental standalone add-on. It
adds `Rivit Concrete Road 45deg`, using copied and re-keyed sets of all 20
Rivit Road S3D/FSH resources with their model vertices rotated 45 degrees in
the tile plane. It reuses the ordinary Road tunnel SC4PATH topology and is
intended to test diagonal-facing portal geometry with the existing functional
tunnel connection.
