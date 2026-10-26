# Homebrew

Monster Builder for Pathfinder 2e can load your own adjustments, templates,
road maps, domains and traits beside the ones it comes with. This page
explains where homebrew lives, how to make it with the editors, and, for
anyone who prefers to write files by hand, every key the files can hold.

You don't need to read past the first part to use homebrew: the editors in
the **Homebrew** menu make the files for you.

- [Making homebrew with the editors](#making-homebrew-with-the-editors)
- [Where homebrew lives](#where-homebrew-lives)
- [When something is wrong](#when-something-is-wrong)
- [Writing files by hand](#writing-files-by-hand)
- [Adjustments and templates](#adjustments-and-templates)
- [Road maps](#road-maps)
- [Domains](#domains)
- [Traits](#traits)


## Making homebrew with the editors

The **Homebrew** menu has an editor for each kind of homebrew:

| Menu item | Makes |
|---|---|
| Adjustments and Templates... | Things you apply to a monster: an ancestry, a "Stonekin" or "Elite"-style change (Creature or Hazard > Adjustments), or a template that remakes it, like a vampire (Creature or Hazard > Templates). |
| Domains... | Domains a Divine Warden can pick, with their domain spells. |
| Road Maps... | Starting benchmarks for a kind of creature (Creature or Hazard > Roadmap). |
| Traits... | Your own traits: a note shown in the form for monsters with the trait, and the skills that identify them for Recall Knowledge. |

Every editor works the same way:

- The list on the left shows your homebrew of that kind, and which folder
  each one is in.
- **New** starts an empty form. **Copy from Built-in...** starts from one of
  the app's own (the built-in one never changes), so you can make a house
  rule version of it.
- **Save** writes the file. A new entry goes in the folder chosen at the
  bottom (the homebrew data folder unless you pick another one); an entry
  you opened is saved back to its own file. The app reloads its homebrew
  each time you save, so menus and the statblock catch up at once.
- **Delete...** asks first, then moves the file to your system's trash (or,
  for a file that holds several domains or traits, takes just that one out).
- Leaving an entry with unsaved changes asks whether to save them.

Problems are listed at the top of the form: **To fix before saving** (it
can't be saved like that) and **Worth checking** (it can be saved, but may
not do what you expect). The fields concerned are outlined in red; point at
one to see its problem.

The **Advanced** box under each form holds anything in the file that the
form doesn't show, exactly as written, so editing a file in the form never
loses part of it. It's usually empty. If a field is greyed out, the file sets
that part in a way the form can't show (for example HP that changes in tiers
by level); it's in the Advanced box instead.

While the **Adjustments and Templates** editor is open, the statblock shows
the open monster with the entry you're editing applied, updated as you type.
Move the editor aside to see it. Your monster isn't changed by this; close
the editor and the statblock is back to normal. If the monster already uses
the entry you're editing, the preview shows your edited version in its
place.

A file that isn't valid JSON (see [Writing files by hand](#writing-files-by-hand))
can't be shown as a form. It opens as text, with the cursor on the line the
problem is on. Fix it there and save, and it opens as a form again.


## Where homebrew lives

Homebrew is read from **homebrew folders**. The first is the homebrew data
folder, which **Homebrew > Open Homebrew Folder** opens (it's made if it
doesn't exist yet). On Linux it is `~/.local/share/pf2monster/homebrew`.

You can add more folders in **Homebrew > Homebrew Folders...**, for example
one per campaign or setting, or a folder a friend shares with you. Each has a
checkbox: untick it to leave that folder's homebrew out for now without
removing it from the list. Folders are read top to bottom, after the
built-in data. The data folder always comes first.

Every homebrew folder has the same layout:

```
homebrew/
  adjustments/   adjustments and templates, one per file
  roadmaps/      road maps, one per file
  domains/       domains, one or more per file
  traits/        traits, one or more per file
```

Inside each of those you can make subfolders to keep things sorted
(`adjustments/my-campaign/stonekin.json`). Only files ending in `.json` are read.

After changing files by hand, use **Homebrew > Reload Homebrew**.

### How monsters remember homebrew

A monster saves the adjustments applied to it by their id. Homebrew ids are
saved with `homebrew:` in front (`homebrew:stonekin`), so a homebrew
entry never mixes with a built-in one of the same id: your own "Elite" and
the built-in Elite can both exist.

If you open a monster where its homebrew isn't loaded (the folder is
switched off, or you gave the monster to a friend without the file), the
monster keeps the entry and the form marks it as not in effect. Nothing is
lost; it works again once the homebrew is back.

To share homebrew, share the files (or the whole folder) along with the
monsters.

### Same names and ids

- A homebrew entry with the same name as a built-in one shows up beside it,
  not in its place.
- Two homebrew files with the same id (or two domains or traits with the
  same name): the first one read wins, and the other is reported as a
  problem.


## When something is wrong

When the app loads homebrew (at start, on Reload Homebrew, or after an
editor saves) and a file has a problem, it says which file, and where in it
when it can:

```
custom-ancestry.json, line 12, column 5: "langauges" in "changes" isn't a key the app
knows, so it's ignored (did you mean "languages"?)
    in /home/you/.local/share/pf2monster/homebrew/adjustments
```

The parts of your homebrew without problems still load. A message after
starting or reloading lists the first problems; every problem also goes to
the problem log, `pf2monster-homebrew.log`, which **Open Log** opens. It's
written to your monster collection folder unless you choose another folder
in Homebrew Folders..., and replaced each time homebrew loads with problems.

Common problems:

| Message | What to do |
|---|---|
| isn't valid JSON ... probably a comma is missing at the end of line N | Add the comma at the end of that line (see below). |
| "x" isn't a key the app knows, so it's ignored (did you mean "y"?) | A typo: rename the key. |
| it needs an "id" / a "name" ... so this file wasn't loaded | Add the missing key. |
| "format": 1 is missing | Add `"format": 1,` as the first line inside the `{`. |
| its id "x" is already used by ... | Give one of the two files another id. |
| "hihg" isn't a benchmark for will | Use one of the benchmarks listed in the message. |


## Writing files by hand

Homebrew files are JSON, a plain-text format. Any text editor can edit them;
save them as UTF-8. If you've never written JSON, the editors are the easier
way; these are the rules if you do:

- A file is one `{ ... }`. Inside, each entry is `"key": value`, with a comma
  between entries but **not after the last one**.
- Text goes in double quotes: `"Stonekin"`. A `"` inside text is written `\"`,
  a backslash `\\`.
- Numbers, `true`, `false` and `null` go without quotes: `5`, `-2`, `true`.
- A list is `[ ... ]` with commas between items: `["primal", "earth"]`.
- An object is `{ ... }` with `"key": value` entries.
- Every file starts with `"format": 1`.

The app's own files (in the source code, under `resources/`) use exactly the
same format, so they make good examples. So does `docs/examples/homebrew/`,
a homebrew folder you can add as it is in **Homebrew > Homebrew Folders...**:
it holds two serpentfolk ancestries (Zyss and Aapoph, in Creature or
Hazard > Adjustments > Ancestry > Serpentfolk), worked out from Monster Core's
serpentfolk, with choices, Strikes and abilities.


## Adjustments and templates

One file each, in `adjustments/`. An adjustment changes a creature (Elite,
an ancestry); a template remakes it (a vampire). Both work the same way;
`"kind"` decides which menu they go in.

### A first example

A Stonekin ancestry for an NPC: it replaces its ancestry trait, adds a language,
darkvision, fire resistance growing with its level, and an ability.

```json
{
 "format": 1,
 "kind": "adjustment",
 "id": "stonekin",
 "name": "Stonekin",
 "group": "Ancestry",
 "rules": "Replaces its ancestry trait with stonekin; adds Petran, darkvision, fire resistance and Rock Hide.",
 "changes": {
  "replaceOrAdd": {"any ancestry": "stonekin"},
  "languagesAlways": true,
  "languages": ["Petran"],
  "senses": ["darkvision"],
  "resistances": [{"type": "fire", "expr": "max(1, level / 2)"}]
 },
 "abilities": [
  {"key": "rock-hide", "name": "Rock Hide", "type": "defense", "actions": "none",
   "description": "{Name}'s rocky skin shrugs off small blows."}
 ]
}
```

### Main keys

| Key | What it is |
|---|---|
| `format` | Always `1`. |
| `kind` | `"adjustment"` (Creature or Hazard > Adjustments) or `"template"` (Creature or Hazard > Templates). Required. |
| `id` | A short name without spaces or colons, unique among your homebrew (`"custom-stonekin"`). Monsters remember it, so don't change it once monsters use it. Required. |
| `name` | What the menu shows. Required. |
| `group` | The submenu it's in (`"Ancestry"`, `"Undead"`); `"Undead/Vampire"` for a submenu inside another. A submenu that doesn't exist yet is made, at any depth (`"Ancestry/My Campaign"` adds My Campaign inside Ancestry). Empty or left out: the menu itself. |
| `prefix` | Put before the monster's name (`"Stonekin"` makes "Stonekin guard"). |
| `prefixInText` | `false`: the prefix shows in the title but not where abilities name the creature (as Elite and Weak do). Default `true`. |
| `rules` | What it does, in a line or two; shown in the status bar. |
| `optional` | What the GM does by hand; a grey note under it in the form. |
| `appliesTo` | `{"min": 3, "max": 20}`: the creature levels it works at (the level so far, before this one's change). Outside them it changes nothing and the form says so. |
| `order` | Its place in the menu among the others (built-in ones use numbers up to about 150; homebrew without it goes after them). |
| `dialog` | `"options"` when it has `choices` (the options dialog opens when it's applied). The editor sets it for you. |
| `changes` | What it changes; see [Changes](#changes). |
| `abilities` | Abilities it gives; see [Abilities](#abilities). |
| `strikes` | Strikes it gives; see [Strikes](#strikes). |
| `choices` | Choices in its options dialog; see [Choices](#choices). |
| `source`, `license`, `note` | For you; the app ignores them. |

### Changes

Everything inside `"changes": { ... }`. Leave out what doesn't change.

**Numbers by level ("steps").** Some changes depend on the creature's level
*before* the change. They are a list of steps; the first one whose `min` and
`max` include the level wins. A step without `min` or `max` covers every
level:

```json
"level": [{"change": 1}]
"hp": [{"max": 4, "change": 10}, {"max": 19, "change": 20}, {"change": 30}]
```

**Typed entries.** Speeds, resistances, weaknesses and HP abilities are
written as a type and an amount. The amount is either steps (as above) or a
formula (`"expr"`, see [Formulas](#formulas)) worked out at the creature's
new level. A `"suffix"` goes after the amount, and `"when"` makes it depend
on a choice:

```json
"resistances": [
 {"type": "fire", "steps": [{"change": 5}]},
 {"type": "physical", "steps": [{"max": 7, "change": 5}, {"change": 10}], "suffix": "(except silver)"},
 {"type": "fire", "expr": "max(1, level / 2)"}
]
```

An entry with only a `"type"` is added as written, without an amount
(`{"type": "strigoi weaknesses"}`).

| Key | What it does |
|---|---|
| `level` | Steps: the level change (`[{"change": 1}]`). |
| `bonus` | Added to AC, attacks, DCs, saves, Perception and skills (`1`, `-2`). |
| `bonusSkip` | Left out of the bonus: `["perception"]`, `["dcs"]`, or both. |
| `strikeDamage` | Added to its Strikes' damage. |
| `spellBonus` | Added to its spell DCs and spell attacks. |
| `hp` | Steps: the HP change. HP never goes below 1. |
| `traits` | Traits added if missing: `["stonekin"]`. |
| `removeTraits` | Traits taken away. |
| `replaceOrAdd` | `{"human": "stonekin"}`: a trait (or the creature type) that becomes another; if the creature doesn't have it, the other is added. `{"any ancestry": "stonekin"}` replaces every ancestry trait it has (see below). |
| `replaceTraits` | `{"animal": "beast"}`: like `replaceOrAdd`, but only replaces. |
| `removeLiving` | `true`: it loses the traits of a living creature (humanoid and the ancestry traits, but not skeleton). |
| `livingType` | With `removeLiving`, the creature type it becomes when its type was one of those (`"undead"`). |
| `rarity` | `"uncommon"`, `"rare"`, `"unique"` (at least that rare), or `"raise"` (one step rarer). |
| `senses` | Senses added after Perception: `["darkvision", "tremorsense (imprecise) 30 feet"]`. |
| `senseUpgrades` | `{"darkvision": "greater darkvision"}`: a sense it has becomes another. |
| `languages` | Languages added, only to a creature that has languages. |
| `languagesAlways` | `true`: add `languages` even to a creature with none. |
| `languageExtras` | `["telepathy 100 feet"]`: added after the semicolon in its Languages line ("Aklo, Common; telepathy 100 feet"), even to a creature with no languages. One it has with a smaller amount is raised. |
| `landSpeed` | `{"set": 25}`, `{"add": 5}`, `{"max": 20}` (lowered to at most) or `{"min": 30}` (raised to at least). |
| `speeds` | Typed: Speeds added, or raised to this (`{"type": "fly", "steps": [{"change": 30}]}`). |
| `derivedSpeeds` | Speeds worked out from others: `{"type": "swim", "from": "land", "half": true, "min": 5, "onlyIfMissing": true}`. `"from"` can be `"fastest"`. |
| `landToFly` | `true`: its land Speed becomes a fly Speed. |
| `size` | The size it becomes: `"tiny"` to `"gargantuan"`. |
| `grow` | `{"sizes": 1, "reach": 5}`: it grows by that many sizes; at Huge or bigger, its melee reach grows by `reach` feet. |
| `reach` | `{"lowest": 0, "others": 5}`: its shortest-reach melee Strikes get the first reach, the others the second (Miniature). |
| `immunities` | Immunities added; a resistance or weakness to the same thing is removed. |
| `resistances`, `weaknesses` | Typed: added, or raised to this amount. A resistance and weakness to the same type cancel out. |
| `hpAbilities` | After its HP: `["void healing"]`, or typed for an amount (`{"type": "fast healing", "steps": [{"change": 5}]}`). |
| `saveNote` | Added to the saves' note: `"+1 status to all saves vs. magic"`. |
| `attributeMin` | `{"intelligence": -3}`: attributes raised to at least this. |
| `attributeSet` | `{"strength": -5}`: attributes set to this. |
| `items` | Items added if missing: `["clan dagger"]`. |
| `skillBenchmarks` | `{"stealth": "high"}`: skills raised to that benchmark for the new level. Also `"primary"` (high, extreme from 11th level with the `primaryExtreme` choice) and `"secondary"` (moderate). |
| `skillsToHighest` | `["stealth"]`: skills set to its highest skill modifier. |
| `statChanges` | `{"will": -1, "athletics": -2}`: statistics changed by an amount when it has them. |
| `lores` | `[{"name": "Stonekin Lore", "benchmark": "high"}]`: a Lore raised to a benchmark, or `"highest"` (its highest skill). |
| `newSkills` | `[{"skill": "deception", "benchmark": "moderate", "ifHigh": "charisma", "better": "high", "note": "{+4} to lie"}]`: a skill it gains if it has none; `better` when the attribute `ifHigh` is high or extreme; `{+N}` in the note is its modifier plus N. |
| `strikeTraits` | `{"all": ["magical"], "melee": ["unarmed"]}`: traits its Strikes gain. |
| `strikeDamageAdd` | `{"all": "plus 1d6 fire", "melee": "plus 2d6 void"}`: added to its Strikes' damage. A damage type as the key adds it only to Strikes of that type, joined to any rider they have: `{"piercing": "plus serpentfolk venom"}` makes "1d6 piercing plus Grab" into "1d6 piercing plus Grab and serpentfolk venom". |
| `strikeDie` | `"fire"`: one die of each Strike becomes this damage type (as the elemental adjustments). |
| `renameStrikes` | `[{"from": "fist", "name": "claw", "damageFrom": "bludgeoning", "damageTo": "slashing"}]`. |
| `convertStrike` | `{"name": "ghostly hand", "attack": 2, "traits": "agile, finesse, magical", "damageType": "void"}`: its best melee Strike becomes this. |
| `editAbilities` | `[{"match": "breath", "rename": "Void Breath", "addDamage": "4d6 void damage", "append": "text", "traits": "void"}]`: its abilities whose name contains `match` are changed. |
| `innateSpells` | `[{"list": "Divine Innate", "spell": "levitate (at will)", "rank": "(level + 1) / 2", "dc": "high"}]`: spells added to a list of that name (made if missing). `"rank": "cantrip"` for a cantrip; `"dc": "own:moderate"` uses its own highest DC; `"attack": true` gives the list a spell attack. |
| `domainSpells` | `{"choices": ["domain1", "domain2"], "list": "Divine Domain", "advancedLevel": 10, "points": [{"max": 9, "change": 1}, {"change": 2}], "dc": "own:moderate"}`: the domain spells of the domains picked in those choices. |
| `recallKnowledge` | `{"difficulty": "incredibly hard", "skills": ["Society"], "when": "abilities:obscura"}`: its Recall Knowledge DC uses this difficulty instead of its rarity's, and these skills are added. |

**Ancestry traits.** "any ancestry" knows the ancestries on Archives of
Nethys (human, elf, dwarf, ..., and grippli for older creatures). Versatile
heritages (changeling, nephilim, ...) aren't ancestries, so they stay. Your
own ancestries count too: every trait an "any ancestry" replacement gives,
so a Stonekin replaces an Elf and the other way round, and every homebrew
trait with `"ancestry": true` (see [Traits](#traits)).

### Abilities

Each ability in `"abilities"`:

| Key | What it is |
|---|---|
| `key` | A short name, unique in this file (`"rock-hide"`). The editor makes one from the name. |
| `name` | The ability's name. |
| `type` | Where it goes: `"general"` (top of the statblock), `"defense"`, or `"offense"`. |
| `actions` | `"none"`, `"one"`, `"two"`, `"three"`, `"one to two"`, `"one to three"`, `"two to three"`, `"free"` or `"reaction"`. |
| `traits` | `"aura, fire"`. |
| `description` | Its text; see [Text placeholders](#text-placeholders). Bold labels like **Trigger**, **Effect**, **Frequency**, **Saving Throw** and **Critical Success** are made bold for you. A new line starts a new paragraph. |
| `when` | Only made when a choice is made; see [Conditions](#conditions). |

An ability the monster already has (same name) isn't added again.

### Strikes

Each Strike in `"strikes"`:

| Key | What it is |
|---|---|
| `key`, `name` | As for abilities. |
| `type` | `"Melee"` or `"Ranged"` (if left out: Ranged). |
| `from` | Where its numbers come from: `"best"` (copies its best Strike of that type), `"lowest"` (its weakest), `"agile"` (its agile Strike's damage, else three-quarters of its best), `"nonagile"` (its best non-agile Strike's), or `"none"` (the benchmarks below). |
| `attack` | Attack benchmark when it isn't copied: `"extreme"`, `"high"`, `"moderate"` (default), `"low"`, or `"best"` (its best melee attack). |
| `damage` | Damage benchmark when it isn't copied: `"extreme"`, `"high"`, `"moderate"` or `"low"` (default). |
| `traits` | `"agile, finesse"`. |
| `die` | The damage die it uses instead (`"d8"`): copied "2d4+9" becomes "2d8+9". |
| `damageType` | `"slashing"`. |
| `extra` | After the damage: `"plus Grab"`. |
| `damageText` | Damage written out instead, with formulas: `"{=max(1, level / 3)}d8 void"`. |
| `fallback` | With `"nonagile"`: when it has no non-agile Strike, its best Strike's damage times this (`1.3333`). |
| `reachPlus` | A reach this many feet longer than its size's. |
| `replace` | `true`: takes the place of a Strike of that name it already has. |
| `mergeExtra` | `true`: if it has a Strike of that name already, that one gets the `traits` and `extra` (a serpentfolk's own fangs get the venom). |
| `when` | See [Conditions](#conditions). |

A Strike the monster already has (same name) isn't added again, unless
`replace` or `mergeExtra` is set. A new melee Strike goes before its ranged
ones, as Paizo prints them.

### Choices

With `"choices"`, applying it opens an options dialog (set `"dialog":
"options"`). Each choice:

| Key | What it is |
|---|---|
| `key` | What conditions name (`"abilities"`). |
| `type` | `"pick"` (several of, as checkboxes), `"choice"` (one of, as a list), `"check"` (one checkbox) or `"text"` (typed text). |
| `label` | Its title in the dialog. |
| `note` | A grey line under it. |
| `items` | For pick and choice: `[{"key": "rock-hide", "label": "Rock Hide", "on": true}]`. `"on"`: ticked at first. An item can have `"tip"` (a tooltip), `"minLevel"` (offered only from that level, after the change) and `"changes"` (changes made only when it's picked, in the same format as the main `changes`). |
| `default` | For check (`true`/`false`), text or choice (an item's key). |
| `placeholder` | For text: grey example text in the box. |
| `minLevel` | For check: offered only from that level. |
| `when` | Offered only when another choice is made. |
| `itemsFrom` | `"domains"`: the items are every domain (built-in and homebrew). |

A typed text choice can be used in texts as `{opt:key}`, and a prefix like
`"Were{opt:animal|lower}"` makes "Werewolf".

### Conditions

`"when"` on an ability, Strike, choice, typed entry or derived Speed makes it
depend on the options dialog:

| Condition | True when |
|---|---|
| `"abilities:rock-hide"` | The item `rock-hide` of the choice `abilities` is picked (or chosen). |
| `"armored"` | The checkbox choice `armored` is ticked. |
| `"!armored"` | It isn't. |
| `"armored&abilities:roll"` | Both. |

### Text placeholders

Ability texts (and traits, names and Strike damage) can hold these; they are
worked out for each monster:

| Placeholder | Becomes |
|---|---|
| `{Name}`, `{name}` | What the monster is called, with or without a capital ("The stonekin guard", or "Sziskazha" for a proper name). |
| `{level}` | Its level after the change. |
| `{halfHp}` | Half its Hit Points. |
| `{=level / 2}` | A number; see [Formulas](#formulas). |
| `{steps\|1d6\|5=2d6\|15=3d6}` | The text of the last step its level reaches. |
| `{dc:standard}` | GM Core's DC for its level (`easy`, `hard`, `very hard`, ... also work). |
| `{spelldc:high}` | The spell DC table at its level (`extreme`, `high`, `moderate`). |
| `{spellattack:high}`, `{skill:high}` | Those tables at its level ("+17"). |
| `{area:unlimited}`, `{area:limited}` | The area damage table's dice at its level. |
| `{statdc:will}` | 10 + its Will (any statistic). |
| `{abilitydc}` | The highest DC in its other abilities, else the high spell DC. |
| `{owndc:moderate}` | The highest DC it lists anywhere, else the spell DC table's. |
| `{strikeattack:best}`, `{strikeattack:claw}` | Its best melee Strike's attack, or its claw's. |
| `{strikedice:claw}` | Its claw's damage dice ("1d8+9"). |
| `{opt:key}` | A choice's text; `{opt:key\|lower}` in lower case, `{opt:key\|cap}` with a capital. |
| `{tradition}` | Its magic tradition (arcane, divine, occult or primal), from a `"tradition"` choice or its traits. |

A lone `%` in a text is the monster's name, as on the website. For a percent
sign, type `\%` in the editor (in a JSON file that's written `"\\%"`, since
a backslash is doubled there).

### Formulas

Formulas (in `{=...}`, `"expr"` and `"rank"`) are whole-number arithmetic,
rounded down: `+ - * /`, parentheses, `min(a, b)` and `max(a, b)`. They can
use `level`, the monster's statistics by key (`hp`, `spelldc`, `will`, ...),
and `resmin` / `resmax` (the resistance table's ends for its level).

`max(1, level / 2)` is half the level, rounded down, and at least 1.


## Road maps

One file each, in `roadmaps/`. A road map sets the benchmarks a creature of
a role starts from (Creature or Hazard > Roadmap) and shows notes in the form while
it's applied.

```json
{
 "format": 1,
 "kind": "roadmap",
 "id": "custom-sage",
 "name": "Sage",
 "variant": "Wisdom",
 "list": "class",
 "rules": "High Wisdom; high Will; light spells.",
 "benchmarks": {
  "wisdom": ["high", "extreme"],
  "will": ["high"],
  "religion": ["high"]
 },
 "notes": {
  "spells": "Light spells up to {maxrank}."
 }
}
```

| Key | What it is |
|---|---|
| `format` | `1`. |
| `kind` | `"roadmap"`. Required. |
| `id` | As for adjustments. Required. |
| `name` | What the menu shows. Required. |
| `variant` | Optional: road maps with the same name and different variants share a submenu ("Fighter > Strength, Dexterity"). |
| `list` | `"base"` (Roadmap > Base) or `"class"` (Roadmap > Class). |
| `rules` | Shown in the status bar. |
| `order` | Its place in the menu. |
| `benchmarks` | For each statistic, the benchmark it sets, then any alternatives the rules allow (offered in the form's hint). |
| `notes` | Notes shown at the top of the form's sections while the road map is applied. `{maxrank}` becomes the highest spell rank for the monster's level, `{maxrank-1}` the one below. |

Statistics and their benchmarks:

| Statistic keys | Benchmarks |
|---|---|
| `perception`, `fortitude`, `reflex`, `will` | extreme, high, moderate, low, terrible |
| `strength`, `dexterity`, `constitution`, `intelligence`, `wisdom`, `charisma` | extreme, high, moderate, low, terrible, abysmal |
| `ac` | extreme, high, moderate, low |
| `hp` | high, moderate, low |
| `spelldc`, `spellattack` | extreme, high, moderate |
| skills (`acrobatics`, `arcana`, ... `thievery`) | extreme, high, moderate, low |

Perception, attributes, AC, saves and HP that a road map doesn't name go to
moderate; skills it doesn't name stay as they are.

Note sections: `perception`, `attributes`, `skills`, `defense`, `offense`,
`spells`, `specials`.


## Domains

Any number per file, in `domains/`. Homebrew domains are listed with the
built-in ones (alphabetically) wherever a domain is picked, as for the
Divine Warden template.

```json
{
 "format": 1,
 "domains": [
  {"name": "Storm", "spell": "thunderclap", "advanced": "eye of the storm"}
 ]
}
```

| Key | What it is |
|---|---|
| `name` | The domain's name. Required, unique among your homebrew domains. |
| `spell` | Its domain spell. Required. |
| `advanced` | Its advanced domain spell. Required. |
| `source` | For you. |

A monster remembers a homebrew domain as `homebrew:` and its name in lower
case (`homebrew:storm`), so renaming a domain a monster uses breaks the link.


## Traits

Any number per file, in `traits/`. A homebrew trait gives monsters with
that trait (in their Type or Traits) a note in the form, and adds skills to
identify them in their Recall Knowledge line. It can also add to a trait the
app already knows: your note shows beside the built-in one, and your skills
are added to the built-in ones.

```json
{
 "format": 1,
 "traits": {
  "stonekin": {
   "name": "Stonekin",
   "lines": ["**Senses** darkvision", "**Resistances** fire"],
   "recall": ["Society", "Stonekin Lore"],
   "ancestry": true
  }
 }
}
```

| Key | What it is |
|---|---|
| (the key) | The trait as typed in a monster's Traits, in lower case (`"stonekin"`). |
| `name` | How the note and the Recall Knowledge line write it; default: the trait with a capital letter. |
| `lines` | The note, one line each; `**bold**` for labels, as the built-in notes do. |
| `recall` | Skills that identify a creature with this trait. A trait with skills counts as a creature type, so a monster with it gets no "no creature type trait" warning. |
| `ancestry` | `true`: an ancestry, which an adjustment's "any ancestry" replaces. Not needed when an "any ancestry" adjustment gives the trait: that counts already. |
| `source` | For you. |
