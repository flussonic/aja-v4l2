# Counters and state of an SDI node in sysfs

Companion of `sdi_av.h`: that file says what a frame is, this one says what a
node tells about itself between frames. Both are the same, byte for byte, in
every driver that follows the contract and in the client, so one piece of code
monitors any card.

The attributes live next to the node, in
`/sys/class/video4linux/video<N>/`. Every one of them is a plain text file:
readable by anyone, one line, no header, no units, terminated by a newline. A
capture node and a playout node carry the same names; where the meaning of a
name differs between the two, it is said below.

Nothing here belongs to one vendor. A driver is free to add attributes of its
own next to these -- its own settings, its own diagnostics -- and a client that
does not know them ignores them. What a driver may not do is give one of the
names below another meaning.

## No data, no attribute

A driver creates an attribute only when the card can answer it. A counter the
hardware does not report is **not** created, and a client reading it gets
`ENOENT`.

This is the whole point of the rule: zero and "the card does not say" are
different answers, and a file that always reads `0` gives the second while
looking like the first. A permanent zero in a monitoring system is not a
missing measurement, it is a perfect result -- no lost frames, no errors, ever.
A client reads `ENOENT` as "unknown" and says so; it never fills the gap with a
zero of its own.

## Counters

A counter is an unsigned decimal number. It starts at zero when streaming
starts (`VIDIOC_STREAMON`) and only grows while the node streams. A reader that
sees a counter go down has seen a new streaming session, not an error; it
starts its differences over from the new value.

| attribute | capture | playout |
| --- | --- | --- |
| `frames` | frames handed to the client | frames the card has sent |
| `frames_skipped` | frames the card had but the client did not get | frames the card sent again because a new one was not ready |
| `no_buffer` | of those: the client had queued no buffer | of those: the client had queued no buffer |
| `resyncs` | positions the driver had to correct to stay with the card | the same |
| `no_sync` | frames arriving without lock or in another standard | -- |
| `events_missed` | start-of-frame interrupts the driver did not see | the same, on the playout side |
| `crc_errors` | lines whose CRC did not match | -- |
| `dma_errors` | transfers that did not complete | the same |
| `restarts` | times the watchdog restarted the node | the same |
| `underflows` | -- | times the card ran out of data on the wire |
| `anc_dropped` | -- | ancillary packets that did not fit their line |
| `audio_dropped` | -- | audio samples there was no room for |

`frames_skipped` is the one counter the two sides name alike and mean
oppositely: on capture a frame was lost, on playout a frame was shown twice.
Both are the same defect seen from its two ends -- the client did not keep up
with the card -- and `no_buffer` says how much of it was an empty queue rather
than a late one.

On a playout node `frames` counts what went on the wire, repeats **not**
included; `v4l2_buffer.sequence` counts what went on the wire with repeats
included, so the difference between the two is `frames_skipped`. A client that
has both checks them against each other.

## State

State attributes are words, not numbers: lower case, from the dictionary given
here and no other, one per line. A driver that would have to invent a word
outside the dictionary does not create the attribute.

### `signal`

What the connector carries. Free-form on a capture node -- the detected
standard is useful to a person and every card words it differently -- and a
client reads the input state from `VIDIOC_ENUMINPUT.status` and
`V4L2_EVENT_SOURCE_CHANGE`, never from this text.

### `reference`

The state of the board's reference input -- genlock -- as one word:

| word | meaning |
| --- | --- |
| `locked` | a reference signal is present and the card's timing follows it |
| `unlocked` | a reference signal is present and the card's timing does not follow it |
| `no_reference` | no signal on the reference input |
| `unknown` | the card has a reference input but does not say what state it is in |

A card without a reference input does not create the attribute; `unknown` is
for a card that has one and cannot read it, which is a different thing and
worth telling apart.

The detected standard of the reference signal, when the card reports one, goes
to a separate attribute of the driver's own, because it is a text for a person
and not a state for a program.

## What is not here

Settings are not part of this contract. Frame timing, clock adjustment, the 3G
mapping, what the card puts on the wire when nothing is queued -- every card
offers a different subset of those, they are written as well as read, and a
client that has to know which ones exist is back to asking the card by name.
What a client sets per frame it sets in the frame, through the metadata plane
of `sdi_av.h`; what is set once for the node stays a vendor attribute.

The card's identity is not here either: it is `VIDIOC_QUERYCAP` and the media
device (`MEDIA_IOC_DEVICE_INFO`). Its temperature and voltages are a hwmon
device, where a monitoring system already knows to look.
