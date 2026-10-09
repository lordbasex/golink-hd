# HOWTO: a game's art and music made with AI

This is the log of how the art and the music of **ANTÍDOTO** (a 1930s cartoon style run and gun inside the human body, 1 or 2 players) were made with AI tools that cost nothing, written so the next game can follow it, change it, or start from it. Every command and every prompt is here exactly as it was run.

Tools:

| What | Tool | Cost and license | Where it ran |
|---|---|---|---|
| Pictures (characters, enemies, bosses, backgrounds, UI) | ChatGPT's image generation, in a browser where the user is signed in | The user's own ChatGPT plan; the pictures are the user's to use | chatgpt.com |
| Music | [ACE-Step 1.5](https://github.com/ace-step/ACE-Step-1.5), text to music | Free, MIT (code and weights), weights downloaded from Hugging Face with no account | An Apple M1 (16 GB) on the local network |
| Sound effects | To be decided (see [Sound effects](#sound-effects)) | Free | |

Nothing here needs an account to be created, a password typed by an agent, or a payment.

## 1. The game's brief

Decided with the user before any picture:

- **Heroes:** two medicine capsules, **ROJO** (red and white) and **AZUL** (blue and white), like a red pill and a blue pill. One or two players.
- **Villains:** viruses that attack parts of the body.
- **World:** the inside of the human body, climbing from the colon up to the brain, one zone per level; **each level scrolls from left to right** and ends with the zone's boss.
- **Style:** 1930s rubber-hose cartoon animation (ink outlines, flat cel colors, film grain). Original designs only: the era's style can be used, other games' characters cannot (no cups, no mugs, no logos).

| Zone | Scenery | Boss |
|---|---|---|
| Colon | Wavy folds, a murky river, gut bacteria far away | FAGO REX, a bacteriophage with a top hat |
| Small intestine | A tunnel lined with a forest of villi | LOMBRIZ VÍRICA, a virus worm |
| Stomach | A cavern with a lake of acid | ÁCIDO BARÓN, surfing on acid |
| Heart and blood | Red blood cells as moving platforms, valves as doors | REY CÁPSIDE, a crowned virus on a clot |
| Lungs | Alveoli like balloons, gusts of wind | GRIPÓN, a flu virus that sneezes |
| Brain | Neurons with electric sparks | NEUROVIRUS, two phases |

Every prompt sent is below, with the file it made; the pictures still to come follow the same pattern (enemies, bosses, each zone's background layers and tiles, the start, ending and level intro screens).

## 2. Pictures with ChatGPT

### How it was driven

The agent used the user's own Chrome, already signed in to chatgpt.com, through the Claude in Chrome extension (an agent must never sign in, type passwords or create accounts: the user signs in). All pictures go in **one conversation**, so the style and the characters stay the same from picture to picture. Each prompt is typed in the message box and sent with Enter; a picture takes about 1 to 2 minutes. Downloading each picture needs the user's approval.

### The prompts, in the order they were sent

**Prompt 1: the style for the whole conversation, plus the heroes' design sheet.** Result: a 3 × 2 sheet, Rojo on top and Azul below, front, side and back views. Approved by the user ("me encantan los personajes").

```
You are the art director of an original 2D run-and-gun game called "ANTÍDOTO". Style for every image in this conversation: 1930s rubber-hose cartoon animation, hand-inked thick black outlines with slight line wobble, flat cel colors with soft watercolor-like shading, subtle film grain, warm vintage palette. Characters have pie-cut eyes, white gloves, rubbery noodle limbs and big expressive poses. Everything is ORIGINAL: do not imitate any existing game or cartoon character, no cups, no mugs, no logos, no text unless asked. Game world: the inside of the human body, drawn as a whimsical but anatomically inspired cartoon (organs, tissues, cells, vessels), never gory. Sprites: full body, side view facing right, centered, transparent background (PNG), no shadow on the ground, no frame or border. Sprite sheets: frames in ONE horizontal row, every frame the same size, the character the same size and at the same height in every frame, feet on the same line. Reply only with the image. FIRST IMAGE: a character design sheet of two hero pills, front view, side view and back view each: "ROJO", a red medicine capsule (top half glossy red, bottom half white), and "AZUL", a blue medicine capsule (top half glossy blue, bottom half white). Each has a cartoon face on the capsule (pie-cut eyes, big grin), rubber-hose arms with white gloves, short legs with chunky shoes (red shoes for Rojo, blue for Azul). Rojo is brave and cocky, Azul is calm and clever. Same height and proportions. Transparent background.
```

File: `heroes_sheet.png`.

**Prompt 2: Rojo's run cycle.** Result: 8 frames in one row, good squash and stretch.

```
Perfect, keep exactly these two designs. Next image: sprite sheet of ROJO the red pill hero, RUN cycle, side view facing right, 8 frames in ONE horizontal row, exaggerated rubber-hose run with arms pumping and squash and stretch, every frame the same size, the character the same size and at the same height in every frame, feet on the same ground line, evenly spaced, nothing overlapping between frames. Wide image (about 4:1), transparent background.
```

File: `red_run.png`.

**Prompt 3: Rojo idle.**

```
Great run cycle. Next image, same rules (one horizontal row, same size and ground line, evenly spaced, transparent background): ROJO the red pill hero, IDLE animation, 6 frames, breathing and bouncing slightly with gloves on hips, a blink in one frame.
```

File: `red_idle.png`. Result: 6 frames, a blink in the fourth.

**Prompt 4: Rojo's jump.** Result: 6 frames, but drawn along the jump's arc (each frame at another height); the frames are aligned on their feet when they are cut, since the engine moves the character.

```
Next image, same rules: ROJO the red pill hero, JUMP, 6 frames: crouch, take-off, rising, top of the jump (stretched), falling, landing squash.
```

File: `red_jump.png`.

**The weapon (decided with the user):** the heroes shoot with their fingers held like a pistol. The gesture belongs to no one; what makes it this game's own is what comes out of the finger: **antibodies**, small glowing Y shapes (red for Rojo, blue for Azul), since antibodies are what fight viruses in the body. A vintage brass syringe was proposed and turned down.

**Prompt 5: Rojo shooting.** From here on the prompt also says the character stays in place, after the jump's arc.

```
Next image, same rules, and this time the character stays in place (no vertical movement, same ground line in every frame): ROJO the red pill hero, SHOOT, 6 frames, standing and firing forward with the right glove shaped like a finger pistol (index finger pointed forward, thumb up), a small red glowing Y-shaped antibody bullet leaving the fingertip, recoil and a puff of muzzle smoke at the fingertip.
```

File: `red_shoot.png`. Result: 6 frames, the finger pistol and the red Y leaving it with a puff of smoke.

**Prompt 6: Rojo shooting up and diagonally up.**

```
Excellent. Next image, same rules, character in place: ROJO the red pill hero, SHOOT UP and SHOOT DIAGONAL UP, 6 frames: 3 frames firing straight up with the finger pistol pointing up, then 3 frames firing diagonally up-forward, red Y-shaped antibody bullets leaving the fingertip.
```

File: `red_shoot_up.png`.

**Prompt 7: Rojo bored (asked by the user: a yawn when the player waits too long), with its own yawn sound later.**

```
Next image, same rules, character in place: ROJO the red pill hero, BORED IDLE (when the player waits too long), 8 frames: looks around, taps his shoe impatiently, then a big exaggerated yawn with the mouth wide open and arms stretching up, eyes half closed, then back to standing.
```

File: `red_bored.png`.

### The rest of Rojo, in a queue

The remaining animations were sent by a small queue run in the ChatGPT page (the extension's JavaScript tool, with the user's approval to generate and download every picture of this game): it types each prompt in the composer (`.ProseMirror`, with `document.execCommand('insertText', …)`), clicks the button labelled "Enviar" (Send), waits until a **new** picture is in the page and the "Detener" (Stop) button is gone for 8 seconds, and downloads it with its name. Waiting only for the Stop button is not enough: ChatGPT first "thinks" without it, so the first version moved on too early, and it even saved the dash under `red_tired_idle.png` (the newest picture on the page was still the dash). Check that two different prompts never give the same file (`md5 -q source/*.png | sort | uniq -d` prints nothing); a wrong one is downloaded again by finding its picture after its own prompt's text in the page.

| File | Prompt (after "Next image, same rules, character in place: ROJO the red pill hero,") |
|---|---|
| `red_dash.png` | DASH, 6 frames: wind-up crouch, then a fast horizontal dash forward with the body stretched long, speed lines and a dust puff behind, then a skidding stop. |
| `red_tired_idle.png` | LOW HEALTH IDLE (one hit left), 6 frames: exhausted and hunched over, panting with sweat drops, eyes droopy, the glossy red shell scratched, cracked and chipped with flakes peeling off, colors slightly faded, wobbling on tired legs. |
| `red_tired_run.png` | LOW HEALTH RUN (one hit left), 8 frames: a clumsy exhausted jog, hunched, panting with sweat drops, the shell scratched, cracked and chipped with flakes peeling off, colors slightly faded. |
| `red_dissolve.png` | DEATH, 8 frames: the cracked shell splits open, the pill fizzes like an effervescent tablet with bubbles, crumbles and dissolves into powder and foam, and in the last frames a little ghost of the pill floats up waving goodbye. |
| `red_duck.png` | DUCK and DUCK-SHOOT, 6 frames: 3 frames crouching low and squashed, then 3 frames crouching while firing forward with the finger pistol, a small red glowing Y-shaped antibody bullet leaving the fingertip. |
| `red_hurt.png` | HURT, 4 frames: a white flash, knocked back with a shocked face and stars, a scratch appears on the shell, recovering. |
| `red_win.png` | VICTORY, 6 frames: a happy dance, a jump with a fist in the air, blowing smoke off the finger pistol, a proud pose. |
| `red_super.png` | SUPER ATTACK, 8 frames: braces, the top half of the capsule pops open like a lid, a big burst of red glowing medicine granules sprays forward, then the lid closes back with a wink. |

The health states (the user's idea): full health draws the normal sheets; with 2 hits left the engine draws scratches and faded colors over them; with 1 hit left the tired sheets; at 0 the pill dissolves.

### Azul is Rojo recolored

The user's rule: both heroes are the same pictures in their own color. So Azul is never drawn by ChatGPT: [`examples/antidoto/recolor.py`](../../examples/antidoto/recolor.py) turns every `red_*.png` into `blue_*.png`, shifting the hue of the red pixels (hue 340° to 25°, saturation 30 % or more: the capsule's top, the shoes, the nose and the antibodies) to blue (212°) and keeping everything else (the white half, the skin of the face, the gloves, the outlines, the transparency). Both heroes move exactly alike and it costs half the pictures.

```bash
uv run --with pillow --with numpy examples/antidoto/recolor.py
```

### Cutting the strips into engine sheets

[`tools/sprites.py`](../../tools/sprites.py) turns each strip into a sheet of equal frames for the engine, and [`examples/antidoto/cut.sh`](../../examples/antidoto/cut.sh) runs it on every hero strip (`UV=/path/to/uv examples/antidoto/cut.sh 80`). What the pictures needed, found by looking at a preview of every cut sheet on a plain background:

- **Frames touch.** An arm or a puff reaches into the next frame's space, so empty columns do not separate frames; the strip is cut in N equal parts, each cut moved to the emptiest column near its border.
- **Slivers and bullets.** A piece of the neighbour, or a bullet already flying, ends up in a frame; each frame keeps only its character (its biggest blob and what lies near its box). Bullets are drawn by the engine on their own.
- **Each strip is drawn at its own size.** The same character is smaller in the run strip than in the idle one, so neither "the tallest frame is 80 px" nor one scale for all works: every strip is scaled so the **capsule's colored half** (its biggest strongly colored blob, the same in every pose) measures what it does in the idle strip.
- **A jump comes along its arc**; every frame is put on the sheet's bottom, since the engine moves the character.

### Downloading the pictures

ChatGPT shows each picture as a `blob:` URL inside the page (`main img`, the generated ones are wider than 400 px; the animation sheets came 2172 × 724 RGBA with real transparency, the design sheet 1448 × 1086 RGB). With the user's approval, this snippet run in the page (the extension's JavaScript tool) saves them to `~/Downloads` with their names, in the order they appear in the conversation:

```js
const imgs = [...document.querySelectorAll('main img')].filter(i => i.naturalWidth > 400);
const names = ['heroes_sheet.png', 'red_run.png', 'red_idle.png', 'red_jump.png'];
for (let k = 0; k < names.length; k++) {
  const blob = await fetch(imgs[k].src).then(r => r.blob());
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = names[k];
  document.body.appendChild(a); a.click(); a.remove();
  await new Promise(r => setTimeout(r, 1500));
}
```

Then they are moved to the game's folder:

```bash
D=examples/antidoto/source; mkdir -p $D
for f in heroes_sheet red_run red_idle red_jump; do mv ~/Downloads/$f.png $D/; done
file $D/*.png
```

### Lessons so far

- Saying the rules once (prompt 1) and then "same rules" keeps later prompts short.
- Asking for the frame count, "one horizontal row", "same ground line" and "nothing overlapping" gives sheets that can be cut into equal frames.
- "Keep exactly these two designs" after the approved sheet keeps the faces and colors.
- An animation with movement (a jump) comes drawn along its path; say "stays in place, no vertical movement" when the engine moves the character.

## 3. Music with ACE-Step 1.5

### Install (on the M1, once)

ACE-Step 1.5 supports Apple silicon (Metal), Windows, Linux and AMD/Intel GPUs, but **not Intel Macs**: its `pyproject.toml` has no PyTorch for `darwin` on `x86_64`. On an Intel Mac it would only run inside a Linux container on the CPU, very slowly, so it runs on the M1.

`uv` (the Python package manager it uses) is installed as the official binary, checked against its SHA-256, without Homebrew or pip:

```bash
mkdir -p ~/ai/bin && cd ~/ai
V=$(curl -s https://api.github.com/repos/astral-sh/uv/releases/latest | /usr/bin/python3 -c "import json,sys;print(json.load(sys.stdin)['tag_name'])")
F=uv-aarch64-apple-darwin.tar.gz
curl -sL -o $F https://github.com/astral-sh/uv/releases/download/$V/$F
curl -sL -o $F.sha256 https://github.com/astral-sh/uv/releases/download/$V/$F.sha256
shasum -a 256 -c $F.sha256
tar xzf $F && cp uv-aarch64-apple-darwin/uv* bin/
bin/uv --version            # uv 0.12.24 when this was written

git clone --depth 1 https://github.com/ace-step/ACE-Step-1.5
cd ACE-Step-1.5
~/ai/bin/uv sync            # downloads Python 3.12 and every dependency into .venv
```

The first generation downloads the models (DiT `acestep-v15-turbo`, the VAE, the 1.7B language model and a text embedding model) into `ACE-Step-1.5/checkpoints`, several GB, with no account.

### Generating a track

ACE-Step's CLI reads a TOML file (`cli.py --config FILE`); its keys are the ones in `cli.py`'s defaults and in `docs/en/CLI.md`. `thinking = false` skips the language model's rewriting of the caption, so the caption is used as written. A fixed `seed` makes a track reproducible.

**Track 1: the colon's level** (`~/ai/antidoto/colon.toml`):

```toml
caption = "Instrumental 1930s jazz big band for a cartoon video game level: hot swing and ragtime, fast stride piano, muted trumpets with wah-wah, clarinet runs, trombone slides, slap upright bass, brushed drums, playful and mischievous mood with a sneaky swampy groove, vintage scratchy record feel, loopable"
lyrics = "[Instrumental]"
instrumental = true
duration = 60
bpm = 168
keyscale = "F major"
timesignature = "4"
thinking = false
batch_size = 1
save_dir = "/Users/lordbasex/ai/antidoto/music"
audio_format = "wav"
seed = 1930
use_random_seed = false
```

```bash
cd ~/ai/ACE-Step-1.5
nohup .venv/bin/python cli.py --config ~/ai/antidoto/colon.toml > ~/ai/antidoto/colon.log 2>&1 &
```

**First try: out of memory.** On the 16 GB M1 the first run stopped with `RuntimeError: MPS backend out of memory (MPS allocated: 11.74 GiB, other allocations: 10.00 GiB, max allowed: 20.13 GiB)`: the models did not fit in the GPU's share of the unified memory. The fix is to keep the models on the CPU and move each one to the GPU only while it runs; the track was also shortened to 30 seconds for the test. Lines added to the TOML:

```toml
duration = 30
offload_to_cpu = true
offload_dit_to_cpu = true
```

**Second try: it worked.** 30 seconds of 48 kHz stereo WAV in **43 seconds** on the M1 (`Total time: 43.49s`), saved as `music/<uuid>.wav` with seed 1930. It is copied off the M1 and turned into an AAC file to listen to:

```bash
scp lordbasex@192.168.1.85:ai/antidoto/music/<uuid>.wav examples/antidoto/source/music/colon_test.wav
afconvert -f m4af -d aac -b 192000 examples/antidoto/source/music/colon_test.wav colon_test.m4a
```

### The whole soundtrack

The user liked the test track ("me encanta el audio que creaste"), so its caption became the base of every track: the same 1930s big band, each track with its own instruments, tempo, key and mood. [`examples/antidoto/make_tracks.py`](../../examples/antidoto/make_tracks.py) writes one TOML per track (the settings above, a seed per track) and generates them one after another, skipping the ones already made, so it can be run again after a stop:

```bash
scp examples/antidoto/make_tracks.py lordbasex@192.168.1.85:ai/antidoto/
ssh lordbasex@192.168.1.85 'cd ~/ai/ACE-Step-1.5 && nohup .venv/bin/python ~/ai/antidoto/make_tracks.py > ~/ai/antidoto/tracks/all.log 2>&1 &'
```

| Track | Seconds | BPM | Key | Mood |
|---|---|---|---|---|
| title | 90 | 150 | C major | Grand overture, triumphant fanfare |
| map | 60 | 120 | G major | Light walking ragtime |
| colon | 90 | 168 | F major | The test track's swampy hot swing |
| intestine | 90 | 160 | Bb major | Oom-pah tuba, xylophone, wobbly |
| stomach | 90 | 172 | D minor | Jungle jazz, tom-toms, growling trombones |
| heart | 90 | 180 | E minor | Fast swing over a heartbeat kick |
| lungs | 90 | 140 | A major | Airy flutes and harp |
| brain | 90 | 132 | C minor | Eerie musical saw, celesta |
| boss | 90 | 190 | G minor | Frantic hot jazz |
| final_boss | 120 | 176 | D minor | Epic, timpani, dark brass |
| victory | 12 | 150 | C major | Short fanfare |
| game_over | 10 | 80 | C minor | Sad wah-wah trombone |

## Sound effects

The effects the user asked for (the heroes' jump, shot, being hit and a yawn when they stand still too long) and the rest of the game's:

| Group | Sounds |
|---|---|
| Rojo and Azul | Jump, land, shoot, super attack (the capsule opens), dash, hit ("ouch"), knocked out, revive, yawn, victory |
| Viruses | Squash, pop, spit, flying buzz |
| Bosses | Roar, stomp, Gripón's sneeze, hit, defeat |
| Objects | Vitamin, checkpoint |
| Interface | Menu select, "READY? GO!", "KNOCKOUT!" |

Tool: [MOSS-SoundEffect v2](https://github.com/OpenMOSS/MOSS-TTS) (Apache 2.0, a 1.3B flow matching model, up to 30 s at 48 kHz, natural, biological and human action sounds), on the M1 after the music. Effects it cannot make well are synthesized in code, like classic cartoon effects (slide whistles, pops, bonks).

### Install on Apple silicon

Its own README targets NVIDIA cards (`torch==2.9.0+cu128`); on the M1 it runs on Metal with the same torch version from PyPI, in its own environment:

```bash
cd ~/ai && git clone --depth 1 https://github.com/OpenMOSS/MOSS-TTS
cd MOSS-TTS/moss_soundeffect_v2
~/ai/bin/uv venv -p 3.12 .venv
~/ai/bin/uv pip install -p .venv/bin/python -e . torch==2.9.0 torchaudio==2.9.0 torchvision==0.24.0
```

Three things stopped it on the M1, each fixed in turn:

1. `InductorError: float64 is not supported by MPS`: the pipeline compiles the model with `torch.compile`; run it with `TORCHDYNAMO_DISABLE=1`.
2. `Cannot convert a MPS Tensor to float64` and then `ComplexDouble … not supported`: the model computes its time embedding and rotary positions in double precision. In the M1's copy only, those become single precision (`sed -i '' 's/torch\.float64/torch.float32/g'` on `diffsynth/models/wan_video_dit.py` and `wan_audio_dit.py`, and `].double() / dim))` → `].float() / dim))` in both), which does not change the sound.
3. `TorchCodec is required for save_with_torchcodec`: the pipeline's own `save_audio` needs another package, after a whole 9 minute generation; [`make_sfx.py`](../../examples/antidoto/make_sfx.py) writes the WAV itself with Python's `wave` module.

On the M1 (16 GB) each diffusion step takes about 10 seconds, so the default 50 steps took 9 minutes for one effect; the script uses 30.

```bash
scp examples/antidoto/make_sfx.py lordbasex@192.168.1.85:ai/antidoto/
ssh lordbasex@192.168.1.85 'cd ~/ai/antidoto && TORCHDYNAMO_DISABLE=1 nohup ~/ai/MOSS-TTS/moss_soundeffect_v2/.venv/bin/python make_sfx.py > sfx.log 2>&1 &'
```

## Putting it together and watching it

[`examples/antidoto/build.py`](../../examples/antidoto/build.py) writes the package folder (`examples/antidoto/game`): the manifest with the heroes' physics (an 80 px hero: hitbox 28 × 66, a jump of about 12 cells), their skins from the cut sheets, the zone's painted layers (resized to the game's height) and its music, and the level. Then it is packed and played without a screen, with a button script, saving every other frame and the sound, and ffmpeg makes a video of it:

```bash
UV=/path/to/uv examples/antidoto/cut.sh 80
UV=/path/to/uv python3 examples/antidoto/build.py
tools/glhd pack examples/antidoto/game antidoto.glhd && tools/glhd check antidoto.glhd
tools/hdrun ./libgolinkhd.dylib --content antidoto.glhd --frames 1100 --script run.txt --every 2 --audio audio.raw --out frames
ffmpeg -framerate 30 -pattern_type glob -i 'frames/frame-*.png' -f s16le -ar 48000 -ac 2 -i audio.raw \
  -vf scale=1280:720:flags=neighbor -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest antidoto.mp4
```

The first version (2026-10-09): Rojo and Azul running, jumping and yawning in the painted colon with its music; the floor, the enemies and the pickups are still the built-in game's.

### Feedback from playing it

- "The walk is so fast you cannot see the steps" (the user, on play.go-link.org): the run animation went by time (14 frames a second) whatever the speed, so the feet slid over the floor. The engine got `stride`: the run's frames follow the distance walked (one cycle of 8 frames every 130 pixels), and the hero was slowed down for its size (walk 2.4, run 3.8 pixels a frame instead of 3 and 4.6).

- "It still looks the same, and the enemies too; make every object more natural" (the user, after the next video). Looking at the frames one by one showed four causes: the engine's outline drawn around art that has its own ink (a jagged dark halo, now off for this game); every frame was centered on its box, so the body jumped left and right as the limbs moved (the cutter now puts the body's center, its biggest strongly colored blob, in the middle of every cell); the 8-frame run strip was 8 similar poses, not a walk cycle; and the enemies animated by time. ChatGPT was asked again for a real cycle:

```
Next image, same rules, character in place: ROJO the red pill hero, a proper animator WALK CYCLE of 12 frames in ONE horizontal row, every frame the same size, the body at the SAME horizontal position in every frame (the character walks in place, like on a treadmill), feet on the same ground line. Classic cycle, two steps: 1 contact (right foot forward heel down, left foot back), 2 down (weight on the right leg, body lowest), 3 passing (left leg passing the right), 4 up (body highest, pushing off), 5 contact with the left foot forward, 6 down, 7 passing, 8 up, then 9-12 in-betweens that make the loop smooth back to frame 1. Arms swing opposite to the legs, rubber-hose bounce. Each frame clearly different from the next.
```

and the same for the germ (`a smooth ROLLING and HOPPING cycle of 8 frames … the body at the SAME horizontal position in every frame (moving in place) … squash when landing and stretch when hopping`). The lesson for any game: ask for a cycle by its animation poses and "in place", and judge it on a cut sheet before putting it in the game. The hero walks a cycle every 150 pixels, the germ every 70 (its frames follow where it is).

- "None of the three videos looks smooth; can't you see it yourself?" (the user). Measuring the video frame by frame did: the capsule moved about 3 pixels in one frame and half a pixel in the next, and its top went up and down every frame. A probe printing the player's state each frame showed `ground` going 1, 0, 1, 0: with the game's gravity (half a pixel a frame) a standing player sank half a pixel, the engine looked for the floor at the whole pixel and missed it, so every other frame was drawn as a jump. Fixed in the engine for every game (and a second bug it hid: the speed crept past the top speed). The lesson: measure the picture, then print the state, before tuning by eye.

- After the floor fix the user said "much better", but a drawn walk cycle still has only 12 drawings for every speed. The user chose a **rubber-hose puppet**: the body, gloves and shoes as separate pictures, and the engine poses them again on every frame (`rig` in the package format). ChatGPT drew the parts (one queue, `P` being the style prefix below):

```
Next image (same 1930s rubber-hose style, transparent background, everything separated with empty space between pieces, nothing overlapping, no limbs drawn on the body):
red_parts_body.png:  a sheet of ROJO the red pill hero as a PUPPET BODY ONLY: just the capsule (glossy red top half, white bottom half) with its cartoon face, NO arms, NO legs, NO gloves, NO shoes, side view facing right, 6 versions in ONE horizontal row, same size: 1 normal grin, 2 blinking, 3 mouth wide open shouting, 4 hurt with a shocked face, 5 tired with droopy eyes, 6 yawning. All the same size and the same position in their cells.
red_parts_hands.png: a sheet of 6 separate white cartoon GLOVES (rubber-hose style, thick black outline, a cuff at the wrist), in ONE horizontal row, same size, the wrist pointing to the LEFT and the hand to the right: 1 open hand, 2 fist, 3 finger pistol (index forward, thumb up), 4 hand waving, 5 pointing up, 6 open hand seen from the palm.
red_parts_feet.png:  a sheet of 4 separate chunky glossy RED cartoon SHOES (rubber-hose style, thick black outline, like the hero's shoes), side view with the toe to the RIGHT, in ONE horizontal row, same size: 1 flat on the ground, 2 toe pointing down (pushing off), 3 heel down toe up (contact), 4 seen a little from below while in the air.
```

Then Azul's parts are recolored and every piece is cut into the engine's sheets, each with the pivot the engine expects (the glove's cuff on the left edge at mid height, the shoe's ankle opening a third of the way across, the body standing on its bottom), every piece of a sheet scaled by the same factor:

```bash
uv run --with pillow --with numpy examples/antidoto/recolor.py
uv run --with pillow --with numpy --with scipy examples/antidoto/parts.py 52 22 18   # body, glove and shoe heights
UV=/path/to/uv python3 examples/antidoto/build.py   # adds "rig" to both skins
```

- "The black arms are wrong: too close to the mouth, move them further left" (the user, on the first puppet video). The shoulders were in front of the body's middle, so the near arm crossed the face. They moved behind it (the near one a sixth of the body back, the far one two fifths), the hand swings forward less than back, the arms got shorter (15 pixels) so the hands stop at the hip, and the gloves turn along the forearm instead of always pointing forward. "Much better" (the user).

- "With the arms up, how would it look jumping while shooting with the finger? I don't think the arms up is a good idea" (the user). In the air the near arm now goes forward at the hip, ready to shoot, and the far one back for balance; the engine also has the aiming pose for when shooting arrives: the near arm straight forward at the hip with the finger pistol pointing level (where the shots go), in any state, walking, standing or jumping, while the rest of the puppet keeps moving. Every arm stays below the mouth.

- Shooting (the order agreed with the user: the shot first, then health and the worn shell). The engine got `weapon`; ChatGPT drew the antibody and its burst:

```
Next image (same 1930s rubber-hose style, thick black ink outline, transparent background, everything separated with empty space between pieces, nothing overlapping):
red_shot.png:     a sheet of ROJO's bullet: a small glossy RED Y-shaped ANTIBODY (like the letter Y, two short arms and a stem, rounded tips, a white shine, a soft red glow around it), flying to the RIGHT (the stem behind, the two arms in front), 4 frames in ONE horizontal row, same size and same position in every cell: it spins a little and pulses, with 2 or 3 tiny speed lines behind it. Small and readable: the whole Y fits a square.
red_shot_hit.png: a sheet of the antibody's IMPACT when it hits a virus: 6 frames in ONE horizontal row, same size, centered in every cell: 1 a small red flash star, 2 a bigger star burst with tiny red Y fragments, 3 a round cartoon POP cloud with sparkles, 4 the cloud breaking into puffs, 5 small puffs and stars fading, 6 the last tiny sparkles. Red, white and pale pink only.
```

The queue made the first image but stopped before downloading the second, so it was fetched from the page by hand (the newest big `img`, its `src` as a blob, an anchor with `download`). Azul's are recolored, `cut.sh` cuts them (the antibody 18 pixels tall, the burst 40), and `build.py` adds `"weapon": {"button": "run", "rate": 8, "speed": 900, "range": 420, "muzzle": [30, -33], "enemy_health": 3}`. The first test showed the antibodies flying over the germs (the muzzle at the hand was higher than a germ is tall): the aiming hand went lower and a shot hits 8 pixels above or below an enemy. A short script that walks into a pit is not an engine bug: watch where the hero goes before blaming the code.

- Health and the worn shell (the user: "when the pill is badly hurt it should look tired, its shell wearing away until it dies dissolving"). The engine got `health`; the dissolve strip drawn at the start became the knockout, and ChatGPT drew the worn body for the puppet:

```
Next image (same 1930s rubber-hose style, thick black ink outline, transparent background, everything separated with empty space between pieces, nothing overlapping, no limbs drawn on the body):
red_parts_body_worn.png: the SAME sheet as the puppet body of ROJO (the red and white capsule with its face, NO arms, NO legs, NO gloves, NO shoes, side view facing right, 6 versions in ONE horizontal row, exactly the same size, shape and position in their cells as before: 1 normal grin, 2 blinking, 3 shouting, 4 hurt shocked, 5 tired droopy eyes, 6 yawning), but now the capsule shell is WORN OUT after many hits: the glossy red faded and dull, scratches and scuffs, a few small cracks, a little chip missing at the edge, a bandage strip on one crack, a few tiny powder crumbs falling, sweat drops, the faces look exhausted. Same character, same proportions.
```

`parts.py` cuts it like the body (same height), and `build.py` adds `"health": {"hits": 3, "worn": 1, "knockout": 100}`, the `worn` sheet to the rig and the dissolve as the skin's `knockout`. The first try drew the dissolve two and a half times too big: the cutter scales a strip by the median height of the capsule's colored half, and in a dissolve the capsule breaks into crumbs, so the median was tiny. `--shell-frames 1` measures only the first frame, where the capsule is still whole.

- The super attack (the capsule opens and sprays granules, from the user's first brief). The `super` strip drawn at the start shows the capsule opening; the engine throws real granules that hit enemies, drawn from:

```
red_granule.png: a sheet of ONE small medicine GRANULE (a tiny glossy RED round bead, like the little pellets inside a medicine capsule, with a white shine dot and a soft red glow), 4 frames in ONE horizontal row, same size and same position in every cell: it tumbles and twinkles (the shine moves around, a tiny sparkle on frame 3). Very small and readable: the whole bead fits a square.
```

  ANTÍDOTO's buttons became Cuphead's: A (and B) jump, X shoots, Y throws the super once 6 antibodies have hit. Two cutting lessons. The super strip measured as small as its open lid (the capsule's colored half is only the lid while it is open), so it is measured on its first, closed frame (`--shell-frames 1`). Then the user saw it in the video: "you can see the transparency of the hero's cut". The spray of frames 4 and 5 is wider than an eighth of the strip, so the even vertical cuts sliced it with straight edges and left slivers in the neighbors. `--separate` thins the blobs until the frames stand apart, gives every pixel to the nearest frame and lays the frames side by side before cutting: no straight edge anywhere.

- "They are very loud, very over the top; I may replace them with ElevenLabs" (the user, about the effects). Every effect is now at a lower peak (`SFX_PEAK` 13000 in `build.py`), and any WAV dropped in `source/sfx` with the same name replaces one (the build cuts its silence and sets its level).

- The whole body: the engine got `levels` (several levels in one package), and `build.py` puts ANTÍDOTO's six zones in it, bottom up: colon, intestine, stomach, lungs, heart, brain. The colon stays the hand-made first level; the others are made by `made_level` from a fixed seed (the same game on every build): ground broken by gaps a hero clears (2 to 4 cells), platforms with vitamins over some of them, brick steps, germs on the flat stretches, two checkpoints and the goal, longer and with more germs and gaps the higher up. Each zone has its painted layers, its floor (`textures.py` on its tile strip), its intro card, its music and its grade. The package weighs about 137 MB, almost all of it the six songs as WAV.

```bash
for z in intestine stomach lungs heart brain; do uv run --with pillow --with numpy examples/antidoto/textures.py examples/antidoto/source/tiles_$z.png examples/antidoto/textures; done
UV=/path/to/uv python3 examples/antidoto/build.py
tools/glhd pack examples/antidoto/game antidoto.glhd && tools/glhd check antidoto.glhd
```

## Cleaning up

When the game's audio is done, the generated files are copied off the M1 and `~/ai` (the models, the virtual environment and the outputs) is removed there, as the user asked.
