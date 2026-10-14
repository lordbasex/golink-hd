# TELETRABAJO: CÁMARA APAGADA (Remote Work: Camera Off): design

The first game on the 3D part of go-link HD (`docs/3d.md`). It is the exercise
that proves the engine can carry a whole 3D game, and the record of how to
make the next one. This document is the design to approve before any game code
is written. Every later decision that changes it is written back here.

Status: **approved on 2026-10-13**; phase 1 under way. The game's id in
code is `remote` (`src/3d/games/remote/`, `hdrun --demo remote`), so it
never mixes with the engine's own cameras.

## 1. The idea in one paragraph

A remote developer has one working day at home, from 09:00 to 18:00. The
company wants six hours of real work, and the boss watches the chat status.
The flat is full of better things to do: a nap, the console, the fridge, the
balcony in the sun. The player walks the flat in third person and chooses
every minute: work at the desk, or live a little and risk the boss noticing.
The score rewards whoever lives the most without getting fired and without
missing the six hours.

Titles by language: Spanish `TELETRABAJO: CÁMARA APAGADA` (the reference
text, written first), English `REMOTE WORK: CAMERA OFF`, Portuguese
`TELETRABALHO: CÂMERA DESLIGADA`. The game's texts
follow the engine's three languages (`hd_lang`).

Own work only: no code, names, texts, pictures or sounds come from any other
game of this theme. The reference site was only seen for its level of detail.

## 2. Rules of the day

All numbers are integers in the game's state and tuned in one table
(`remote_tuning`), so balancing never touches the logic.

| Thing | Value | Notes |
|---|---|---|
| A day | 09:00 to 18:00 | 9 game hours |
| Real length | 36 000 frames | 10 minutes at 60 fps |
| A game minute | 66 frames | 540 game minutes, 35 640 frames, the rest is the closing bell |
| Work goal | 360 game minutes | 6 hours at the desk |
| Free time | 180 game minutes | what the player can spend without missing the goal |
| Energy | 0..1000 | starts at 700 |
| Suspicion | 0..1000 | starts at 0; at 1000 the player is fired at once |

### Work

- Sitting at the desk with the computer on counts work minutes.
- The work speed depends on energy:
  - above 300: 1 minute per minute;
  - 100 to 300: half speed;
  - under 100: the character falls asleep on the keyboard. That adds no work and raises suspicion.
- Working drains energy (2 per game minute).

### Energy

Activities give energy back (section 4). Walking costs nothing, so the flat
never feels like a punishment.

### Suspicion

The boss's suspicion rises in three ways:

| Cause | Rise |
|---|---|
| Away from the desk | 2 per game minute after the first 10 minutes away (the chat status turns "away") |
| A boss event missed (section 5) | 150 to 300, by event |
| Asleep on the keyboard | 3 per game minute |
| The boss hears the house on a call (section 5) | 100 each thing, once a call |
| The cat logs the worker off (section 5) | 100 |

It falls in two ways:

| Cause | Fall |
|---|---|
| Working | 1 every 3 game minutes |
| A boss event answered in time | 25 to 50 (section 5) |

These numbers were set in phase 6 with `make remotebots` (section 13).

### The end of the day

At 18:00 the day ends. The day can end three ways:

| Ending | When | Result |
|---|---|---|
| Fired | suspicion reaches 1000 | the game ends at once, with no score beyond what was earned |
| Missed hours | 18:00 with less than 360 work minutes | half the activity points, no bonus |
| Day survived | 18:00 with 360 work minutes or more | full score (section 6) |

## 3. The house

The game is played in the demo's house (`room3d.c`): 200 m², 1600 × 1250
cm, on the top floor of a city block. It replaced the 108 m² flat on
2026-10-13, at the user's request, so that every room has space and a clear
use. Its rooms:

| Row | Rooms |
|---|---|
| North (z 800..1250) | Bedroom (500 × 450, the double bed, the wardrobe), bathroom (250 × 450, the tub, toilet, sink), office (400 × 450, the desk with two screens, a reading corner), game room (450 × 450, the TV and consoles, a beanbag, an arcade cabinet, the guitar) |
| Middle (z 650..800) | Laundry (250 × 150, the washing machine and the dryer, a sink), the hall with the front door at its east end |
| South (z 0..650) | Living room (900 × 650: the dining table by the west window, the lounge with the L sofa facing the TV), kitchen (700 × 650) |
| Outside | A 9 × 2 m terrace along the living room: a sun lounger, a parasol, chairs, plants, a drying rack |

The kitchen is a family's:

- an L of counters with the washing machine under the counter, the dishwasher, the sink under the window, the hob with its oven and hood, and the coffee corner (the machine, mugs, toaster, kettle, microwave);
- the wall cupboards, the two-door fridge, the pantry, the tall cupboards with a second oven;
- an island with three stools, a table for four;
- the bar to the living room in the opening of their shared wall;
- the everyday things on the counters: knives, spoons, a cutting board with bread, pans on a rail, herbs, a fruit bowl, spices, the bins.

Changes for the game:

- Every activity object gets a fixed **interaction spot**: where the
  character stands, which way it faces and how far the prompt reaches
  (about 80 cm).
- The desk's two screens light up when the computer is on, with code
  scrolling in the texture.
- The flatmate goes away (the day is the player's alone). It may come back
  later as a random event, and is the second player of the second version.
- A cat lives in the house, built from the same parts idea as `actor3d`:
  - it wanders between three spots;
  - it is a random event (section 5).

## 4. Activities

The player can do 13 activities besides working, and two more exist only
while their event lasts. Each one has its place, a length in game minutes,
energy and points:

| # | Activity | Room | Minutes | Energy | Points |
|---|---|---|---|---|---|
| 1 | Nap | bed | 30 | +400 | 300 |
| 2 | Coffee | kitchen machine | 3 | +120 | 30 |
| 3 | Cook lunch | kitchen stove | 25 | +300 | 250 |
| 4 | Raid the fridge | kitchen | 5 | +80 | 60 |
| 4b | Eat at the table | kitchen table | 20 | +250 | 200 |
| 5 | Play the console | game room TV | 20 | -50 | 350 |
| 6 | Watch a series | living room sofa | 25 | +100 | 250 |
| 7 | Sunbathe | terrace lounger | 15 | +150 | 200 |
| 8 | Water the plants | terrace | 5 | 0 | 80 |
| 9 | Shower | bathroom | 15 | +200 | 150 |
| 10 | Put on the washing | laundry | 5 | 0 | 100 |
| 11 | Play the guitar | game room | 15 | +50 | 220 |
| 12 | Pet the cat | wherever it rests | 3 | +60 | 50 |
| 13 | Open the door | hall, front door (only while the bell rings) | 2 | 0 | 100 |
| 14 | Put on the headphones | office desk (only while the drill goes) | 1 | 0 | 0 |

The washing machine keeps spinning for 45 game minutes after it is loaded,
and the boss can hear it (section 5).

Each activity:

- only counts its points the first two times (the third and later give 20 %),
  so the best day mixes everything;
- can be cut short with the action button (it gives its share of points and
  energy);
- shows the body in a pose of `actor3d`: sit, lie, type, hold or crouch.

Working at the desk has no length and no points: it adds work while the
player stays (section 2) and costs energy. Asleep on the keyboard (energy
under 100), the worker's head rests on the desk and the screens go dark.

Each activity's place (`activities` in `remote.c`) has two points in
centimeters of the plan:

- **Start:** where the worker stands to start it. The prompt shows within 75 cm, and every start point is checked to be walkable.
- **Body:** where the body is while doing it (on the bed, on the sofa, in the tub), and where it faces.

When the activity ends, the worker is back at the start point.

## 5. Events

### Boss events

The boss sends 4 to 6 events a day: the daily meeting at 10:00 and 3 to 5
more, picked and timed from the day's seed. Each one arrives as a message on
the phone in the HUD with its sound (section 8), and the player has a time
limit to be **awake at the desk**. The phone shows the time left as a
shrinking red bar.

| Event | Limit | Then on the call | Missed | Answered |
|---|---|---|---|---|
| Daily meeting (10:00) | announced at 09:40, at the desk by 10:00 | 15 game minutes ("in a meeting") | +250 | -40 |
| Surprise video call | 45 real seconds | 10 game minutes | +300 | -50 |
| Urgent message | 30 real seconds | none: the answer is enough | +150 | -25 |
| Screen share | 20 real seconds | 5 game minutes | +200 | -40 |
| Production down, a hotfix | 40 real seconds | 20 game minutes deploying ("deploying"); then +150 points | +350 | -30 |
| "Can you work this weekend?" | 30 real seconds, then an answer at the desk: A "of course" (-60, the weekend sold: -250 points at the end) or X "I have plans" (+60) | none | +150, and it counts as a yes | see the limit |
| Video call with the camera on | 30 real seconds | 8 game minutes: the boss also sees the cat on the desk, the flatmate in the office, the worker asleep (+100 each, once) | +300 | -40 |

The code's table (`boss_events`) keeps the first design's calm (-80, -100, -50, -80); `REMOTE_ANSWER_CALM` (50 %) halves it, which is what the table above shows.

A day has at most one weekend question and one hotfix. Between events the
boss also writes things that need no answer (log your hours, a meeting to
plan meetings, can the AI do your ticket), every 40 to 80 game minutes, so
something is always going on.

During any call a window at the top shows it: the boss talking in a little
video and the time left; with the camera on, the worker's own picture with
a red dot (the cat in it when it is on the desk); else "your camera: off". A
hotfix shows the deploy instead: a bar, the step it is on (`git push -f`,
`npm run build`, `kubectl apply`, `pray.sh`) and the percent.

Limits are in real seconds because they are about the player's reaction, not
the story's clock. The meeting is announced 20 game minutes ahead (about 22
real seconds), since 5 game minutes are less than 6 real seconds. A boss
event that comes while another one is under way, or while the internet is
down, waits 5 game minutes.

While a call lasts the worker stays at the desk (the button does nothing but
take the cat off the keyboard). What the boss can hear is the house, each
thing once a call, +100 suspicion each:

- the washing machine spinning (45 game minutes after it is loaded): the classic;
- the bell, when a delivery was not opened (it comes back during the next call);
- the flatmate in the office;
- snoring, when the worker falls asleep on the keyboard;
- the neighbor's drill, without headphones: only a joke from the boss, the drill is not the worker's fault.

### Random events

Two to four a day, all different, from the seed:

- **Delivery at the door:** the bell rings. Opening it within 30 real seconds
  (the door's activity) gives 100 points; otherwise the courier leaves and
  rings again during the next call.
- **The cat on the keyboard:** the cat walks to the office and lies on the
  keyboard. No work counts while it is there; the action button at the desk
  takes it off. After 20 real seconds it types something to the boss and
  logs the worker off: +100 suspicion.
- **The neighbor's drill:** for 30 game minutes the work speed is halved,
  except with the headphones (on their stand on the office desk).
- **The internet drops:** 10 game minutes of no work and no suspicion. It is
  a free break, and the boss cannot see the status either.
- **The flatmate comes back early:** it comes in through the front door and
  walks between five spots of the house for the rest of the day. It gets in
  the way (the worker cannot walk through it, and it waits when the worker is
  in its way) and talks at the worker whenever it is within 2 m.

### The cat

The cat lives in the house all day, from the morning on the bed. It walks
(`play3d_route`) between five spots (the living room rug, the bed, the sun on
the terrace, the kitchen, the game room), jumps up where the spot is high
(the bed, the desk), and rests 20 to 45 seconds at each, lying or sitting.
While it rests the worker can pet it.

### The phone

The phone keeps the last 8 messages (text ids, never strings) with who wrote
them (the boss, the worker, the cat, the flatmate, the house) and the game
time; the HUD shows the last three, the newest bright, and blinks its edge
when one arrives. The chat status the boss sees is shown by the clock:
online, away (more than 10 game minutes from the desk), on a call, in a
meeting, offline (no internet).

Everything random comes from `hd_state.rng`, so a day with the same seed and
the same input is the same day on every computer. That is what lets the lab
replay bots. Each new day mixes into the seed how long the player took on the title,
the pause or the end (section 7).

## 6. Score

At the end of the day:

- **Activity points:** the sum from section 4.
- **Survival bonus:** 1000 if the day was survived.
- **Discretion bonus:** `(1000 - final suspicion)`, only if the day was
  survived.
- **Overtime penalty:** working more than 7 hours costs 2 points per extra
  minute (the game laughs at whoever does it).

| Ending | Title by score |
|---|---|
| Fired | always "Despedido" (Fired) |
| Missed hours, any score | "Becario del sofá" (Sofa intern) |
| Survived, under 1500 | "Empleado ejemplar" (Model employee); the joke is that it is not a compliment here |
| Survived, 1500 to 3000 | "Ninja del teletrabajo" (Remote ninja) |
| Survived, over 3000 | "Leyenda del modo ausente" (Legend of the away status) |

The best score is kept by the host (the engine has no disk), and is shown on
the title screen when the host passes it in.

## 7. Screens

| Screen | Contents |
|---|---|
| Title | The house from above, turning slowly, the worker typing at the desk and the cat on the bed; the game's name in two lines; a menu (Start the day, How to play, Language) moved with the D-pad or the stick and chosen with A or START; the language (English, Español, Português) changes with left and right or A, starting at the host's (English by default); the session's record; "Pulsa START" (press start). After 10 s without input the demo plays (below). |
| Demo | A bot plays 50 seconds of a day with the game's own rules: it walks with `play3d_route` to a coffee, the desk, the sofa, the fridge, the console, the guitar, and does a little of each. A "DEMO" banner is shown; any button brings the title back. |
| How to play | Three pages over the darkened picture: the goal (6 h, do not get fired), the bars, the buttons. A turns the page; the last one (or START) goes back to the title or to the pause. |
| Morning | 08:59: the alarm rings with the worker in bed (the clock blinks, the text shakes), it sits on the bed's edge, gets up, and the camera flies from the bed to its place behind it (4.5 seconds). A or START skips it. The day starts with the worker by the bed. |
| The day | The HUD over the 3D picture (below). |
| Pause | START during the day: Continue, How to play, Restart the day (a new morning), Back to the title. The clock stops. |
| End | Fired: the screen goes black and the boss's last message is typed out first. Then the reason, the rows counting up one every half second, the total counting up, the title by score, and "¡Nuevo récord!" or the record. START: another day (a new morning); A: back to the title. |

Every new day (from the title, the pause or the end) mixes how many frames
the player took into the seed, so the events are different every time while
a save state or a replay is still exact.

The record is kept for the session in the game's state (`best`), so it
survives another day and a save state. Keeping it between sessions needs
the host (go-link) to store it and pass it in: that is phase 7.

### HUD

- Top left: the clock (`11:42`), with the day's progress as a thin bar.
- Top right: three bars with icons:
  - work hours (`4 h 10 min / 6 h`);
  - energy;
  - suspicion (green to red; it shakes over 800).
- Bottom center: the prompt when near an object (`Ⓐ Echarse una siesta` "take a nap").
- Right side: the phone with the last messages of the boss and the events,
  like a chat, with the countdown when there is a limit.
- While an activity runs: its name, a progress ring, and the button to stop.

The HUD is drawn at the logical screen's scale times `hd_res` with the
engine's text (`text.h`), over the finished 3D frame.

## 8. Sound

Recorded sound, not synthesized: music and effects are WAV files in the
game's package (`"game": "remote"`, its `audio` section; README, "game and
audio"), like ANTÍDOTO's. The engine's built-in game (no package) is
silent except for a few built-in blips in the menus. What sounds comes from
what changed in the state from one frame to the next (`remote_sound` at the
end of `hd_remote_step`), so the rules never wait for the sound and a save
state or a replay sounds the same.

### Music

| Track | When |
|---|---|
| `title` | The title, its demo and how to play |
| `day` | The day; after a call it goes on where it was (`music_resume`) |
| `call` | While on a call with the boss (meeting, call, screen share) |
| `end` | The results, after the end's jingle |

The morning has no music: only the alarm. Chill-out and lo-fi: soft beats,
calm, music to work to.

### Effects

Names the game asks for (`hd_effect`); loops go on while what makes them
lasts, each on a channel of its own, fading in and out, placed by where it
is (full within 2 m of the worker, fading to a floor at 10 m, left or right
as the camera sees it).

| Name | Loop | When |
|---|---|---|
| `alarm` | yes | The morning, until the worker sits up |
| `phone_ring` | yes | The boss waits for the worker at the desk (a meeting only in its last 15 s); heard from the desk |
| `message`, `boss_message` | | A message on the phone (the boss's own sound); an urgent message pings again every 2.5 s |
| `call_answer`, `call_missed` | | The worker answers; the boss gives up |
| `heard` | | The boss heard the house on a call |
| `tick` | | Each of the last 5 seconds to answer |
| `doorbell`, `door` | | The delivery (every 4 s while it waits; again during a call if it was missed); the front door opens |
| `drill` | yes | The neighbor's drill, on and off; quiet with the headphones |
| `washer` | yes | The washing machine, from its place |
| `headphones` | | Put on |
| `net_down`, `net_up` | | The internet drops and comes back |
| `meow`, `purr`, `cat_keys` | `purr`, `cat_keys` | The cat goes for the keyboard (and a cross one when taken off, now and then one when it arrives somewhere); petting; the cat walking on the keys |
| `step` | | The worker's and the flatmate's steps, each half of the walk's cycle, never two alike |
| `typing`, `snore` | yes | Working at the desk; asleep on it or napping |
| `coffee`, `cook`, `fridge`, `eat`, `tv`, `console`, `guitar`, `birds`, `water`, `shower` | yes | While doing that activity |
| `heartbeat` | yes | Suspicion over 800, louder the closer to 1000 |
| `done` | | An activity paid points |
| `ui_move`, `ui_select`, `ui_back`, `pause` | | The menus |
| `typewriter`, `count` | | The end: the boss's last words typed; the rows counting up (higher as they go) |
| `fired`, `missed_hours`, `survived`, `record` | | The end's verdict, and a new record |

The recordings, their authors and licenses (only CC0, public domain, or
CC-BY with credit) are listed in `examples/remote/audio.json`, and
`examples/remote/build.py` turns them into the package (CREDITS.md).

## 9. Controls

| Input | Action |
|---|---|
| Left stick or D-pad | Walk (relative to the camera); the stick's tilt sets the speed |
| Right stick or L / R | Turn the camera |
| A/B (the engine's main action) | Interact, start or stop an activity |
| X/Y (the engine's run button) | Run |
| Y | Look at the phone (pauses nothing; shows the whole chat) |
| START | Pause |

The keyboard comes from the host's usual mapping, as for every go-link HD
game, so nothing in the game knows about keyboards.

## 10. Humor and texts

- **Tone:** dry, office sarcasm in neutral Spanish ("tú", no voseo, no
  regional slang), never cruel and never about real companies, products or
  people.
- **Sources of jokes:**
  - the boss's messages;
  - the activity's names and their closing lines;
  - the ending titles;
  - the cat;
  - the status bar (`Ausente`, away; `Reunido`, in a meeting; `En una llamada`, on a call).
- **Size:** about 120 texts in all, each written in Spanish first and then
  translated to English and Portuguese. Texts live in one table in the game's
  source (`remote_text.c`), one row per text with its three languages. This is
  the same layout as ANTÍDOTO's dialogs, so translators touch only that file.

## 11. How it is built on the engine

### Where the code goes

| Part | Files | Reusable |
|---|---|---|
| Follow camera (behind the character, pulled in front of walls by a ray over the voxel grid) | `src/3d/play3d.c` | yes |
| Walk collision (a floor grid of 10 cm cells made from the solids, sliding along walls) | `play3d.c` | yes |
| Routes for what the game moves (the cat, the flatmate, bots) | `play3d.c` (`play3d_route`) | yes |
| Interaction spots and prompts | the game (`activities` in `remote.c`) | no, for now |
| HUD (bars, phone, prompt, progress, the end) at `hd_res` scale | the game (`remote.c`); shared helpers in `src/3d/hud3d.c` when a second game needs them | not yet |
| More poses (lie, hold, crouch) | `actor3d.c` | yes |
| The cat | `pet3d.c` | yes |
| The game: rules, events, score, screens | `src/3d/games/remote/` | no |
| The flat's changes | `room3d.c` | shared with the demo |

The game is a built-in game like the demos: `golinkhd_load_demo(e, 3)` and
`hdrun --demo remote`. The reusable parts are what the next 3D game starts
from. A package format for 3D games (scenes and rules in a file, as format 3
is for 2D) comes after this game. Willy Maker comes last, as always.

### State and save states

Every field stays a 32-bit integer in `hd_state`. The game needs about 300
words:

- the clock;
- the bars;
- the character and the cat;
- the activity under way;
- events and their timers;
- the phone's last 8 messages (as text ids, never strings);
- the counts per activity;
- the score;
- the screen.

To keep `sizeof(hd_state)` and `HD_STATE_VERSION 7` unchanged, so every
ANTÍDOTO save state keeps loading, the game's state shares memory with the
2D level's coin bitmap `taken[]` (3 584 words), which a 3D game never uses:

```c
hd_remote *g = HD_GAME3D(s, hd_remote); /* the bitmap's words, seen as the game's struct */
```

`HD_GAME3D` checks at compile time that the struct fits in the bitmap and
has only 32-bit fields. Only one of them is alive at a time (`show.on` 3 is
the 3D game), and a save state of either is the same bytes. A loaded save
state brings the game's fields back into range like every other field
(`hd_remote_sanitize`). The compatibility check (`docs/3d.md`) proves ANTÍDOTO is
unchanged after the change. If the game ever needs more than the bitmap's
room, that is the moment to bump the version on purpose.

### Speed

The game runs at 720p, like every 3D game (`docs/3d.md`, Performance):

| Part | Budget |
|---|---|
| 3D frame | about 9.5 ms |
| HUD | under 0.5 ms |
| Rules | under 0.1 ms |

That leaves room in the 16.6 ms frame.

### Tests and the lab

- **Same day twice:** a bot plays a whole day; two runs give the same hash.
- **Save state:** a save state at noon continues exactly.
- **Bot strategies:** three bots play whole days in `tests/`:
  - the workaholic (survives, low score);
  - the lazy one (gets fired);
  - the balanced one (survives, high score).

  They are the balance check: if a tuning change lets the lazy bot survive,
  the test fails.
- **ANTÍDOTO:** the compatibility hashes, unchanged.

## 12. Phases

Each phase ends with renders or a video for the user, and waits for approval
before the next one.

| # | Phase | Contents | Rough time |
|---|---|---|---|
| 1 | Walk the flat (**done 2026-10-13**) | Follow camera, collision, character control, run, the state in the coin bitmap's words, the compatibility check | 1 session |
| 2 | Interact (**done 2026-10-14**) | Spots and prompts, the 12 activities with poses, energy and work, the desk's screens | 1–2 sessions |
| 3 | The day (**done 2026-10-14**) | Clock, suspicion, boss events, random events, the cat, the phone | 1–2 sessions |
| 4 | Screens and HUD (**done 2026-10-14**) | Title, how to play, morning, pause, end, score, record | 1 session |
| 5 | Sound (**done 2026-10-14**) | Recorded effects and music (chill-out, lo-fi) in the game's package (`audio`), the alarm, phone, bell, drill; the language in the title's menu | 1 session |
| 6 | Polish (**done 2026-10-14**) | Balancing with bots that walk the house (`make remotebots`), texts in 3 languages checked to fit, things in the hands, the shower's water, docs (`docs/3d.md`'s steps 12 to 18 for the next 3D game) | 1 session |
| 7 | go-link | Show it in go-link Console as an HD game (picture, trailer) | later |
| 8 | Willy Maker | Whatever it needs from this game | last |

## 13. Progress

**More furniture (2026-10-14).** From the same Quaternius pack: the TV unit
(the long one with a gap in the middle), the sideboard (two drawers on
legs), the oval dining table, the floor lamps (the model's base and pole,
the game's own lit shade over the model's), the six kitchen stools and the
bathroom's wall sink (only its pipe is solid, so the floor under the basin
is not darkened). The bookshelves (their books are the game's own), the
bathtub with the shower (its box is wider than a tub) and the kitchen's
counters (full of the game's things) stay built. 27 of 27 places reached.

**The desk made by artists (2026-10-14).** The desk is what the game shows
most, so it came next, from Poly Pizza (CC0): CreativeTrio's desk (a drawer
unit on one side), monitors, mouse and computer, Quaternius's office chair
and desk lamp. Each fits the built piece's box; the screens keep the game's
own glowing pictures of code, put just in front of the model's screen; the
bulb still glows inside the lamp's shade; the computer stands under the desk
with a blue light, only with the package (the built flat stays as it was, so
its pictures do not change). The keyboard stays built: its glow is better
than the models found. The user saw the worker sitting with no keyboard or
mouse: the models' monitor stands covered them. The screens moved to the back
of the desk, the keyboard (now with its keys) and the mouse on its pad to the
front edge, and the model got its own typing pose: it sits 14 cm forward on
the seat, the left hand on the keys and the right on the mouse, palms down
(found by rendering it from the sides and from above). `tools/remotebots --content`: 27 of 27 places
reached, the careful player survives every day.

**Furniture made by artists (2026-10-14).** From Quaternius's "Ultimate House
Interior Pack" (the CC0 copy on OpenGameArt, OBJ files): the L sofa (mirrored
to match the flat's), the armchair, the dining chairs, the double bed and its
nightstands, the toilet, the washing machine and the dryer, the fridge. Each
fits the box of the piece it replaces, so the plan and the game's places do
not move; the tub with its shower, the desk with its screens and the plants
(already detailed) stay built. `tools/remotebots 20 --content remote.glhd`:
27 of 27 places reached, the careful player still survives every day.

**Models made by artists (2026-10-14, phase "models").** The user asked for a
character and things that look much better than the built boxes, from
open-licensed 3D models. What was searched and why (the same rules as the
sound: the files ship in a public game):

- Free CC0: Quaternius's characters and animals, through Poly Pizza and OpenGameArt (Quaternius's own site changed its license in August 2026 to one that forbids passing the files on; the older CC0 copies stay CC0), Kenney's Furniture Kit, KayKit. Sketchfab only with its CC0/CC-BY filter (and a login). Magnific/Freepik and Sketchfab's store licenses: no. A shop's paid characters (threedee.design): their license lets a game use them but not pass the original files on; possible if the user buys one, with only the converted model in the package.
- Picked: Quaternius's "Casual Character" (the worker) and "Hoodie Character" (the flatmate), CC0, about 5,800 triangles, 22 bones kept, clips Idle, Walk, Run, Wave, Interact. Their cat was blockier than the built one, so the built cat stays; Kenney's furniture was simpler than the built furniture.
- The engine got `model3d` (docs/3d.md); the package's `"models"`; `actor3d_place_model` (the clips, and the pose's angles on the bones for sitting, typing, lying, holding, the guitar). The first try opened the arms when lying (an artist's rest is an A): each limb now hangs straight before its angle.

**More of the boss (2026-10-14, after phase 6).** The user asked for a video
call that shows, and for events like "production is down, deploy a fix",
"can you work this weekend?" and "share your screen". Added (section 5): the
hotfix, the weekend question with its two answers, the camera call that sees
the house, the call's window, and the boss's chatter. The guitar is held
for real now (`actor3d_pose.strum`: one hand along the neck, the other over
the body; the angles were found by rendering it from the front and the side
until the hands met it). `make remotebots` after the change: careful 98 %
survived, casual 68 %, slacker fired 18 %, workaholic short of the hours
(and its weekend sold).

**Phase 6, polish (2026-10-14).**

Balance, with `make remotebots` (`tools/remotebots.c`): bots that play a
day like people do, walking the house along `play3d_route`, reacting to the
boss after a delay, resting when tired, answering the door, taking the cat
off the keyboard and putting the headphones on, in four styles, over many
days (each day half a second without drawing).

Before (the numbers of phases 2 and 3), 200 days each:

| Style | Survived | Missed the hours | Fired | Score (median) | Peak suspicion (median / 90 %) |
|---|---|---|---|---|---|
| Careful | 95 % | 2 % | 2 % | 2387 | 29 / 266 |
| Casual | 76 % | 22 % | 2 % | 2269 | 42 / 449 |
| Slacker | 0 % | 90 % | 10 % | 1141 | |
| Workaholic | 0 % | 100 % | 0 % | 78 | |

The styles ended as the design wants (rest and work and you survive; never
work and you miss the hours; never rest and you fall asleep on the
keyboard), but the boss was no threat: a careful player's suspicion peaked
at 29 of 1000, because a minute of work took 1 away and an answered event
50 to 100. Three variants were played; the middle one was chosen (working
calms 1 every 3 minutes, `REMOTE_WORK_CALM_EVERY`; an answered event half
of its table, `REMOTE_ANSWER_CALM`; away 2 a minute, `REMOTE_AWAY_RISE`),
50 days each:

| Style | Survived | Missed the hours | Fired | Score (median) | Peak suspicion (median / 90 %) |
|---|---|---|---|---|---|
| Careful | 94 % | 2 % | 4 % | 2390 | 60 / 554 |
| Casual | 68 % | 24 % | 8 % | 2267 | 96 / 846 |
| Slacker | 0 % | 86 % | 14 % | 1149 | 538 / 1000 |
| Workaholic | 0 % | 96 % | 4 % | 78 | 68 / 401 |

The hardest variant (calm every 5 minutes, 30 %, away 3) fired the slacker
42 % of the days: too much for a game about getting away with it.

Also:

- Things in the hands (`actor3d_pose.prop`, `room3d_prop`): a mug, a pan, a can, a bowl, a controller, a guitar (its neck up and swaying with the strum), a watering can, the parcel, the headphones.
- The shower's water: see-through streaks falling around the worker, who now stands under the shower's head inside the tub.
- Every text, in the three languages, checked to fit the screen at the size it is drawn (a test).
- `docs/3d.md` steps 12 to 18: bots, measuring on the same load, things in the hands, moving effects, sound from what changed, texts that fit, a video for each phase.

Checks: `make test` (the rule test's numbers now follow the tuning macros), `make NO_3D=1 test`, the CI hashes and ANTÍDOTO unchanged, `make bench` the same as the commit before on the same load.

**Phase 5, the sound (2026-10-14).**

What was built (section 8): the game's music and effects come in its
package (`"game": "remote"` with `"audio"`), recorded, every one in the
public domain (CC0). The user asked for chill-out and lo-fi music and for
real sound rather than synthesized; the music was first going to be made
with ACE-Step as ANTÍDOTO's, but the M1 that runs it was off, so free music
was looked for instead and the user picked it.

How the sound was found and put together, to do it again for another game:

1. A list of what the game needs to hear, named (section 8), before looking.
2. Sources whose license lets a free game carry the files in a public repo:
   CC0 first (OpenGameArt filtered by CC0, BigSoundBank, Kenney's packs,
   Freesound sounds marked CC0), CC-BY only with credit. Never "non
   commercial" or "no derivatives" (the loops are cut and re-encoded), never
   Pixabay (its license forbids passing the files on nearly as they are).
3. Every chosen recording in `examples/remote/audio.json`: its page, file,
   author, license, and how to cut it (`start`, `length`, `burst` for one
   sound of a recording of several, `loop`, `seamless`).
4. `python3 examples/remote/build.py fetch` downloads them into `source/`
   (Freesound's originals need a login; its public previews, the same sound
   as MP3, are used); `python3 examples/remote/build.py` cuts, levels, loops
   and stores them as ADPCM, writes the manifest with the credits in three
   languages and `CREDITS.md`; `tools/glhd pack examples/remote/game remote.glhd`.
5. Nobody listened before choosing: jingles were picked by measuring them
   (length, major or minor, the melody going up or down), and the video goes
   to the user to judge by ear.

Checks:

- `make test`: a package with named effects and tracks; the title's track, the alarm looping in the morning and gone when the day starts, a loop no other effect can take the channel of, a call's track and the day's back where it was, the pause silencing the loops, bad packages refused; the language line; the credits (from the menu, cut short, after a day won); credits per language.
- ANTÍDOTO's frames and sound are the same with the engine before and after (360p, 720p, 1080p); the CI hashes (walk, showcase, API) are unchanged.
- The video's sound never clips.

**Phase 4, the screens (2026-10-14).**

What was built (section 7): the title with its menu and the record, the demo
(a bot playing with the game's rules), how to play in three pages, the
morning, the pause, and the end counting up with the title and the record.
Every new day gets its own seed. `hd_remote_new_day` starts a day straight
away for tests and tools; the game itself starts at the title
(`hd_remote_start`).

Checks:

- `make test` goes through every screen: the title's menu, the help pages, the demo starting by itself and stopping on a button, the morning ending by itself, the pause stopping the clock, "continue" and "restart the day", the end; each one drawn once. The session's record survives another day.
- ANTÍDOTO's hashes are unchanged; `make NO_3D=1 test` passes.

**Phase 3, the day (2026-10-14).**

What plays:

- The clock runs from 09:00 to 18:00 (35 640 frames) with a thin bar of the day under it.
- Suspicion (section 2) rises away from the desk, asleep on it, with missed events, with what the boss hears and with the cat; it falls while working and with answered events. At 1000 the worker is fired at once.
- The boss's events and the house's (section 5), planned from the seed when the day starts: the meeting at 10:00, 3 to 5 more from the boss, 2 to 4 from the house, spread from 10:15 to 17:35.
- The cat wanders the house and can be petted; the delivery, the drill and its headphones, the internet cut and the flatmate.
- The end of the day: fired, hours missed (half the points) or survived (the bonuses and the overtime cost), with the title by score; START starts another day. The screen itself is phase 4's.

On screen:

- The HUD gained the chat status by the clock, the suspicion bar (it shakes over 800), and the phone on the right with the last three messages and the countdown when the boss waits.
- The end of the day: the reason, the title, the numbers and the total.

New in the engine:

- `src/3d/pet3d.c`: the cat (`room3d_cat()` is the house's, a ginger tabby).
- `play3d_route`: the shortest way through the walk grid, for the cat, the flatmate and bots.

Fixes to the house found on the way (by the new test that walks to every place):

- A parcel and the umbrella stand closed the way to the front door.
- The pouf stood between the sofa and the armchair, so the sofa was reached only through the terrace; the armchair moved 50 cm north.

Checks:

- `make test`: suspicion away from the desk, a call missed and one answered (with the washing machine heard), no leaving a call, the cat on the keyboard, the three endings, START for another day, and a whole lazy day twice (the same day, fired before 18:00). Every place the day uses is walked to from the hall at the worker's size.
- Whole days without drawing: the one who never works is fired at 16:34, the one who never rests falls asleep at the keyboard and misses the hours, the one who works with coffee breaks survives.
- 720p: 11.6 ms a frame with the cat and the flatmate; the rules take 0.01 ms.
- ANTÍDOTO's hashes are unchanged.

**Phase 2, doing things (2026-10-14).**

What plays:

- The 13 activities above, with their prompts (`A: Echarse una siesta`), poses and progress bars.
- Energy that comes as an activity goes, so stopping early keeps it.
- Points for each activity, cut to a fifth after the second time.
- Work at the desk at full or half speed, or nothing when asleep.
- A closing line with humor for each activity, or "again?" when it is repeated too often.

On screen:

- The HUD: the clock, the work done against the 6 hours, the energy, the points, the prompt or the activity under way, and the messages.
- The desk's two screens scroll code while working (`tex3d_terminal`, redrawn as the state says) and sleep otherwise.

New poses in `actor3d`: `hold`, `crouch` and `lie` (the whole body turned around the hips).

Fixes to the house found on the way:

- The fridge, the reading armchair, the arcade cabinet, the amplifier, a sideboard and a game-room bookshelf were turned or placed wrong.
- Every dining chair faced away from its table.

Checks:

- `make test` plays an activity three times (the repeat rule), stops one halfway, and works at three energies.
- A bot played a stretch of the day with the stick (coffee, fridge, the desk) for the progress video.

**The house (2026-10-13).** The flat became a 200 m² house (section 3). Every room, the laundry and the terrace are reached from the hall (checked with a flood of the walk grid); the bathroom's sink and tiles, a dining table and the game room's pictures were found in the way and moved. 720p: about 11 ms a frame with other work running.

**Phase 1, walking (2026-10-13).**

What was built:

- `src/3d/play3d.c`: the reusable walking pieces (`docs/3d.md`, Walking).
- `src/3d/games/remote/`: the game's state, start, rules, drawing and texts.
- `golinkhd_load_demo(e, 3)` and `hdrun --demo remote` start it.

How it plays:

- The worker walks at 1.5 m/s and runs at 3.5 m/s.
- It turns toward where the stick points, slides along walls and finds doorways.
- The camera follows at 3.8 m, tilted about 55 degrees, and rises over walls.

Changes to the flat for walking (before the house replaced it):

- The dining table is against the north wall with three chairs, and the sofa and coffee table moved 20 cm south.
- The bathroom's door moved to 600..690, with the sink beside the toilet and the tiles cut at the door.

Every room and the balcony are reached from the hall.

Checks and numbers:

- 720p: 9.6 ms a frame on average.
- `make test` covers it: the same walk twice, a save state, never out of the flat.
- ANTÍDOTO's hashes are unchanged.

Controls (the engine's buttons):

| Input | Action |
|---|---|
| Left stick or D-pad | Walk |
| `RUN` (X/Y) | Run |
| L / R or the right stick | Turn the camera |
| The right stick up and down | Tilt the camera |

The main action (`JUMP`, A/B) is for interacting, from phase 2 on.

## 14. Decisions

Answered by the user on 2026-10-13:

1. The name is `TELETRABAJO: CÁMARA APAGADA`.
2. A day lasts 10 real minutes (section 2).
3. The cat is in the first version.
4. The first version is for one player. The second version adds a second
   player: the flatmate, who also works from home (split screen, its own
   desk in the living room, its own bars and boss).
