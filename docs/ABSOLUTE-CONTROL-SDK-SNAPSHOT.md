# Absolute Control SDK snapshot

AbsoluteHOTAS vendors the Control provider ABI header so it can build and remain fully usable
without Absolute Control installed or present in the source tree.

## Current snapshot

- Source repository: `SultanDesync/Absolute-Control`
- Source working tree: commit `9190f3d` plus the finalized append-only
  RecordCollection/action-confirmation, page-open, and pinned-context ABI-v1 tails
- Source path: `include/AbsoluteControlPanelAPI.h`
- Vendored path: `include/AbsoluteControlPanelAPI.h`
- Finalized host source-tree provenance:
  `E326414FE44E85F5B47D73B2E1E4F061DBF204043845774F07511E0DEF1AE358`
- ABI version: `1`
- SHA-256: `A9E3097E9C80902EDE908017C8F7E8E51147A3F0285FEEDED33CF2C48322F540`
- Adopted: 2026-08-20

The vendored file is byte-identical to that source header. Update it only from an identified SDK
revision, record the new commit and hash here, and update the ABI layout fixture in
`tests/absolute_control_subscriber_test.cpp` in the same change. Do not normalize subscriber
headers with an unrecorded file copy.

## Experimental live-component snapshot

Live range meters and telemetry plots use Control's separate additive experimental ABI; they do
not extend or change the stable configuration ABI above.

- Source working tree: `Absolute-Control` at commit `9190f3d`
- Source path: `include/LiveComponentsExperimentalAPI.h`
- Vendored path: `include/LiveComponentsExperimentalAPI.h`
- Experimental ABI version: `1`
- Source/vendored Git blob: `922233abd8a12bb42becc04a59e59f9ce071fd37`
- SHA-256: `6D61DD4E38F19BB1113C7CB0547DF33CB6C1E554604D7F9F6F3AC2C9B8997DA4`
- Adopted: 2026-08-20

The source header had uncommitted experimental-tail work in that identified working tree, so the
content hash is the exact provenance authority. The vendored file is byte-identical. Experimental
registration remains fail-optional: a host without the export still receives the stable HOTAS
settings pages and gameplay continues unchanged.

This snapshot adds size-gated live presentation flags, a pinned/collapsed-secondary vocabulary,
and the append-only dynamic range-frame tail. An older host receives descriptors with the new
presentation bits cleared; its original frame size also prevents the provider from writing the
new tail, preserving the v1 live callback layout in both directions.

## Experimental semantic-composition snapshot

The Flight Axes tab uses Control's separately negotiated C2 semantic-composition ABI to arrange
existing controls and live channels. It does not alter the stable configuration ABI.

- Source working tree: `Absolute-Control` at commit `9190f3d`
- Source path: `include/AbsoluteControlCompositionExperimentalAPI.h`
- Vendored path: `include/AbsoluteControlCompositionExperimentalAPI.h`
- Experimental ABI version: `1`, product capability checkpoint `C2`
- SHA-256: `468CC7737A63EF1128F059541658F89DD3755763F59BBDCF4C4551DFD60092A7`
- Adopted: 2026-08-20

The source header contains the current uncommitted C2 vertical-slice work, so the content hash is
the provenance authority. The vendored file is byte-identical. Registration is fail-optional and
requires the exact C2 capability subset plus successful live-channel registration; older hosts
continue to render the ordinary flat Flight Axes page.

## Adoption scope

This snapshot includes the append-only ABI-v1 definitions for structured layout, provider-owned
binding capture, conflict resolution, bounded RecordCollection metadata, and host-owned action
confirmation, the asynchronous provider page-open command, and the capability-gated pinned
editing-context strip. AbsoluteHOTAS uses the
binding-capture tail for its 53 fixed static HOTAS targets, the selected-record/action tail for
provider-owned dynamic workflows, and the page-open tail to route Toggle Wizard to
`absolute.hotas/hotas-setup` and the two in-module Flight Axes/Ship Buttons deep links. Its pinned
strip exposes the shared edit target, activation behavior, and modifier binding on every rich page;
older hosts receive the ordinary unpinned page set. Older hosts
remain supported through descriptor-size and capability gates; an unavailable or rejected open
command retains the embedded-workbench path and truthful read-only link guidance.

The experimental composition snapshot is used for the Flight Axes and Ship Buttons enhancements.
Flight Axes publishes 76 bounded nodes and 10 associations; Ship Buttons publishes 93 bounded
nodes. Supporting hosts synthesize the three stable pinned-context controls into older complete
page compositions without requiring provider-specific layout nodes. HOTAS
still owns values, capture, transactions, telemetry preparation, and persistence; Control owns
layout, focus, scrolling, and rendering.

Host capabilities are size-gated. The current pages require labeled choices; a base-size/older host
that cannot advertise that capability is treated as an incompatible optional editor. Registration
fails closed without changing AbsoluteHOTAS configuration, DirectInput polling, or gameplay
initialization.
