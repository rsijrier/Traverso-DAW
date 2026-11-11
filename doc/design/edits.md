<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Edits: van invoer naar history step

## Flow

```
invoer (muis/toets) → dispatcher → tool ─┐
agent / edit-bestand ────────────────────┤
                                         ▼
  edit (bv. TSplitEdit)     specifiek: rekenen, voorcontroles, rapport
                                         ▼
  changes (TClipChange)     eenduidige lijst: verwijder X, voeg A en B toe
                                         ▼
  poort (per undo-stack)    algemeen beleid per geraakt object
                                         ▼
  history step              redo(): bijwerkingen (DiskIO, resources manager)
                                         ▼
  undo-stack                dispatcher of agent pusht
```

- Tool: alleen invoer. Virtuele cursor, snap, preview/live. Lost bij commit
  de zachte selectie op naar ids.
- Edit: puur rekenen op ids. Overgeslagen object → rapport met id en reden,
  nooit stil overslaan.
- Poort: eigen object per undo-stack (nu per song), eigendom van de song,
  niet in TSheet. Kent de pool, de sessie en de undo-stack.
- History step: bezit nieuwe objecten tot ze zijn toegevoegd.

## Controles: wie bewaakt wat

| Soort                   | Waar      | Voorbeeld                         | Bij fout        |
|-------------------------|-----------|-----------------------------------|-----------------|
| Domeingrens             | Model     | fade past in clip, stuk ≥ 1 frame | assert (bug)    |
| Algemeen beleid         | Poort     | gelockt, neemt op                 | weigeren, reden |
| Specifiek voor de edit  | Edit      | splitspositie binnen de clip      | rapport, reden  |

Toets voor een domeingrens: moet hij ook gelden bij laden, undo en
splitsen? Ja → model. Nee → poort of edit.
Laden en undo gaan niet langs de poort.

## Adressen

- Gelijken (song, track, master, clip, marker, send, source, plugin):
  hun id, via de pool.
- Onderdelen (gain-, fade-, automation-curves, ruler, plugin chain):
  (eigenaar-id, rol). Een edit kan nooit om de eigenaar heen.

## Overgang

Controles verhuizen pas als de edit komt die ze overneemt (TSplitEdit,
TMoveTool, trim). create_command_mutate_clips() verhuist als eerste naar
de poort, samen met TSplitEdit.

## Open

- Heeft het project een eigen undo-stack (project-master, project-ruler)?
  Dan daar ook een poort.
- Edits over songs heen (twee stacks)?
- TCommitResult::failed() krijgt een id voor het rapport per object.
- Naam van de poort.
