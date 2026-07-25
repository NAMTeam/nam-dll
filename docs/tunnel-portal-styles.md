# Tunnel portal façade styles

The tunnel portal tool discovers façade styles as SC4 exemplars. A style
package can be submitted independently; it does not need to edit a shared
index resource.

## Style exemplar TGI

- Type: `0x6534284A` (Exemplar)
- Group: `0x4A7B6E40` (NAM tunnel portal façade style)
- Instance: any unique ID chosen by the style author

## Properties

| Property | ID | Type | Required | Meaning |
| --- | --- | --- | --- | --- |
| Exemplar Name | `0x00000020` | String | yes | Name shown in the selector |
| Schema Version | `0x4A7B6E41` | Uint32 | yes | Must be `1` |
| Network Type | `0x4A7B6E42` | Uint32 | yes | SC4 network enum; Avenue is `6` |
| Portal Tile Count | `0x4A7B6E43` | Uint32 | yes | `1` or `2` |
| Portal Exemplar 0 | `0x4A7B6E44` | Uint32 | yes | Canonical first facade half |
| Portal Exemplar 1 | `0x4A7B6E45` | Uint32 | two-tile only | Canonical second facade half |
| Icon Group | `0x4A7B6E46` | Uint32 | no | Group of an optional four-frame PNG icon |
| Icon Instance | `0x4A7B6E47` | Uint32 | no | Instance of the optional four-frame PNG icon |

For schema version 1 the replacement portal exemplars must preserve the
native portal path topology. Authors should clone the corresponding base
network portal exemplars and change only façade/model resources. The DLL
substitutes these exemplar IDs inside the native tunnel-piece insertion call;
rotation, height, path initialization, endpoint pairing, and traffic
notification remain native to the selected network.

For two-tile styles, the two exemplar properties currently retain the source
portal pair's canonical north/south order. Style authors should preserve that
source order rather than sorting the exemplars by ID.

The runtime transform uses direct property order for north/south mouths and
swapped property order for east/west mouths. A full Avenue direction-pair
matrix rendered all straight and perpendicular combinations correctly with
that rule, for both Maxis and Rivit examples. Highway and Ground Highway styles
still need equivalent validation before assuming their native sequence arrays
behave identically.

The replacement exemplar is also associated with a same-instance SC4Path
resource in the example package. Swapping the two properties can therefore
swap both the facade model and the Avenue occupant's one-way path set. The DLL
still keeps SC4's native sequence index for its tool-owned rotation and height
arrays, but style authors must treat the two replacement exemplars as complete
model/path halves.

Packages with malformed properties or a network/tile-count mismatch are
ignored and logged. An omitted icon is valid; the selector uses the style
name as its button caption.

Icons use the native SC4 toggle-button bitmap layout: four equal-width
horizontal frames for the normal, pressed, selected, and disabled states.
Each frame should be 89x58 pixels, producing a 356x58 PNG. Frames may be
identical when a style does not need state-specific artwork.

## Proof-of-concept styles

`resources/tunnel-portal-styles/NAM_TunnelPortalStyleExamples.dat` contains
isolated Maxis and Rivit Concrete styles for Avenue, Road, and Rail, plus two
Maxis highway-facade options for Avenue. It is deployed beside the selector
UI DAT for local testing.

The examples do not reference the shared Maxis model IDs at runtime. Each
model's complete zoom/rotation S3D and FSH set is copied to NAM-owned IDs,
and the S3D material IDs are re-keyed with the textures. This is required to
keep the Maxis style visually Maxis when an override mod such as Rivit's
Tunnel Mod is installed.

The Road and Rail styles use cloned native one-tile paths. All four Avenue
styles use cloned native two-tile Avenue paths, including the styles whose
facade geometry originated from a highway portal.

`resources/tunnel-portal-styles/NAM_TunnelPortalStyleRivitExtra.dat` extends
Rivit Concrete coverage to OneWayRoad (network 10), Ground Highway (network 12),
Raised Highway (network 2), and single-track Rail (network 1). It is generated
by `tools/tunnel-style-gen` directly from Rivit's Tunnel Mod: the tool copies
each portal's S3D/FSH/path to NAM-owned instances, rekeys the S3D material ids
to the copied textures, and clones each network's native portal exemplar with
`ResourceKeyType1` repointed at the copied model. The single-track Rail style
wears Rivit's STR concrete face on the rail portal exemplar with a custom
centered path derived from the STR path's class-1 (single-track) strokes.
The Highway and Ground Highway styles ship but still need the in-game
placement/orientation validation noted above.
