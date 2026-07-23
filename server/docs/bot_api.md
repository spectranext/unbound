# BOT API

Bots expose a small API to the ZX Spectrum computer attached to them. Programs can use it in two ways:

- `IN` and `OUT` ports for small state reads and commands.
- XFS files under `/api` for map snapshots and larger structured data.

All port values are one byte, `0..255`. All XFS text files use CRLF line endings: `\r\n`.

## Ports

| Port | Name | Direction | Description |
| ---: | --- | --- | --- |
| 100 | `PORT_API_VERSION` | read | API version. Currently `1`. |
| 101 | `PORT_CURRENT_MODE` | read | Current API mode: `0` idle, `1` moving, `2` collecting, `3` error. |
| 102 | `PORT_MOVE_RIGHT` | write | Move right by the written amount. |
| 103 | `PORT_MOVE_LEFT` | write | Move left by the written amount. |
| 104 | `PORT_MOVE_UP` | write | Move up by the written amount. |
| 105 | `PORT_MOVE_DOWN` | write | Move down by the written amount. |
| 106 | `PORT_MOVE_JUMP` | write | Jump. The written value is ignored. |
| 107 | `PORT_MY_HEALTH` | read | Bot health, clamped to `0..255`. |
| 108 | `PORT_MY_ENERGY` | read | Bot energy, clamped to `0..255`. |
| 109 | `PORT_ENEMIES_NEARBY` | read | Number of enemy objects within the object scan radius, clamped to `0..255`. |
| 110 | `PORT_FRIENDLIES_NEARBY` | read | Number of friendly objects within the object scan radius, clamped to `0..255`. |
| 111 | `PORT_CURSOR_X` | write | Signed X cursor offset from the bot. Values `128..255` mean `-128..-1`. |
| 112 | `PORT_CURSOR_Y` | write | Signed Y cursor offset from the bot. Values `128..255` mean `-128..-1`. |
| 113 | `PORT_COMMAND` | write | Execute a command at the cursor location. |
| 114 | `PORT_BLOCK_ID` | read/write | Select a block id, or read the block id collected by the last collect command. |
| 115 | `PORT_INVENTORY_COUNT` | read | Count of inventory items matching the currently selected `PORT_BLOCK_ID`, clamped to `0..255`. |

### Port Commands

Set `PORT_CURSOR_X` and `PORT_CURSOR_Y`, then write one of these values to `PORT_COMMAND`.

| Value | Command | Description |
| ---: | --- | --- |
| 1 | Move | Move to `bot position + cursor offset`. |
| 2 | Collect | Collect the block at `bot position + cursor offset`. The target must be adjacent. On success, `PORT_BLOCK_ID` becomes the collected block id. |
| 3 | Place | Place the block selected in `PORT_BLOCK_ID` at `bot position + cursor offset`. |

Failures set `PORT_CURRENT_MODE` to `3`.

Example:

```basic
10 PRINT IN 100
20 OUT 111,1: REM cursor x = +1
30 OUT 112,0: REM cursor y = 0
40 OUT 113,2: REM collect adjacent block
50 PRINT IN 114
```

To write a negative cursor offset, write the two's-complement byte. For example, `255` means `-1`.

```basic
10 OUT 111,255: REM cursor x = -1
20 OUT 112,0
30 OUT 113,3: REM place selected block to the left
```

## XFS Files

Use `%cat`, `%fopen`, `%seek`, `INPUT #`, and `PRINT #` with these paths.

| Path | Access | Size | Description |
| --- | --- | ---: | --- |
| `/api` | read directory | - | Lists mounted API files. |
| `/api/blocks` | read | `2205` | 21x21 block snapshot around the bot. |
| `/api/objects` | read | variable | Nearby object snapshot. |
| `/api/location` | read | `14` | Current bot map location. |
| `/api/size` | read | `14` | Current map size. |
| `/api/move` | write | `0` | Write absolute target coordinates; command runs when the file is closed. |

Opening a snapshot file captures a snapshot at that moment. Reads from the same open file continue through that snapshot.

### `/api/blocks`

`/api/blocks` is a fixed-size 21x21 array centered on the bot. The center record is the block where the bot is standing.

Each block is exactly 5 bytes:

```text
NNN\r\n
```

`NNN` is a zero-padded block type number from `000` to `255`. Blocks outside the map, empty blocks, or unknown blocks are `000`.

The data is row-major: top row left to right, then the next row. The first record is relative coordinate `(-10, -10)`, and the center record is `(0, 0)`.

Record index:

```text
index = (relative_y + 10) * 21 + (relative_x + 10)
byte_offset = index * 5
```

Example: read the block directly to the right of the bot.

```basic
10 LET RX=1: LET RY=0
20 LET I=(RY+10)*21+(RX+10)
30 %fopen #4,"/api/blocks","r"
40 %seek #4,0,I*5
50 INPUT #4,B
60 %close #4
70 PRINT B
```

### `/api/objects`

`/api/objects` lists up to 10 closest objects within 64 blocks of the bot. Objects are sorted closest first.

Each object has five CRLF-separated fields:

```text
<object-type>\r\n
<object-id>\r\n
<x>\r\n
<y>\r\n
<enemy>\r\n
```

`enemy` is `1` when the object has a different team from the bot. Otherwise it is `0`.

This file is variable-length and is intended to be read sequentially.

```basic
10 %fopen #4,"/api/objects","r"
20 INPUT #4,T$
30 INPUT #4,ID
40 INPUT #4,X
50 INPUT #4,Y
60 INPUT #4,E
70 %close #4
80 PRINT T$,ID,X,Y,E
```

### `/api/location`

`/api/location` contains two fixed-width numbers:

```text
XXXXX\r\n
YYYYY\r\n
```

The file is always 14 bytes. Values are zero-padded to 5 digits.

```basic
10 %fopen #4,"/api/location","r"
20 INPUT #4,X
30 INPUT #4,Y
40 %close #4
50 PRINT X,Y
```

### `/api/size`

`/api/size` has the same format as `/api/location`, but contains map width and height:

```text
WWWWW\r\n
HHHHH\r\n
```

The file is always 14 bytes.

### `/api/move`

Write two absolute map coordinates, then close the file. The bot starts moving when the file closes.

The coordinates may be separated by spaces, commas, or newlines.

```basic
10 %fopen #4,"/api/move","w"
20 PRINT #4;"00120"
30 PRINT #4;"00040"
40 %close #4
```

This is equivalent:

```basic
10 %fopen #4,"/api/move","w"
20 PRINT #4;"120,40"
30 %close #4
```

If parsing fails, `PORT_CURRENT_MODE` becomes `3`.

## Notes

- `INPUT #` expects line-separated values, so numeric files are formatted with CRLF after every number.
- Fixed-size files are useful with `%seek`; variable-size files should be read sequentially.
- Port commands are immediate. File commands usually run when the file is closed.
- `PORT_BLOCK_ID` is an item/block icon id, not a file offset.
