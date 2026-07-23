# Unbound

<video src="https://github.com/user-attachments/assets/d3bd2049-ec60-4845-9266-b45195438a0e" controls muted playsinline></video>

Unbound is a multiplayer survival automation game for the ZX Spectrum, Spectranet, and Spectranext.

Players land on a hostile alien planet as contracted workers for an interplanetary company. The job is not heroic exploration: it is keeping an industrial outpost alive, solvent, and productive under pressure. Air, heat, power, shelter, logistics, and cash flow all matter.

The game is designed around long-running shared sessions. Players can drop in, check the colony, repair or improve automation, fulfill contracts, and leave the outpost running for the next crew.

Read the original public announcement: [Unbound, the game](https://blog.spectranext.net/unbound-the-game).

## Gameplay

Unbound combines survival, mining, automation, and multiplayer base management.

- Mine raw resources from the planet.
- Refine materials into useful products.
- Keep oxygen, power, heat, and shelter online.
- Build and maintain industrial systems.
- Complete contracts to keep the company funded.
- Buy missing supplies through the Star Store when capital allows.
- Defend the outpost from a planet that does not welcome you.

The main measure of survival is whether the company can stay operational with positive cash flow. Contracts provide the pressure; automation is how the outpost survives it.

## Automation

Most colony infrastructure can be operated manually, but manual work does not scale. Unbound includes programmable in-game computers that can control equipment such as:

- Stationary computers
- Factories
- Oxygen tanks
- Power stations
- Doors
- Forward operation bases

These computers are themselves 48K ZX Spectrum machines. You can program them with BASIC, or use other approaches, to interact with IO ports and automate colony tasks. Need a steady supply chain or a mining bot? Write one.

## Multiplayer

Unbound is built for cooperative play over Spectranet or Spectranext. A game server can host a persistent world that multiple players join from physical hardware or emulators.

The intended rhythm is asynchronous and practical: join the world, inspect what changed, improve the systems, solve immediate problems, and let the server continue.

## Starting the Server

Install Docker (with Docker Compose), then start Unbound in the foreground:

To run a fully local stack with the game server:

```sh
make local@up
```

This exposes the game on TCP `13390`, TNFS on UDP `16384`. The TNFS service hosts the server browser; 

To stop the local stack:

```sh
make local@down
```

To reconfigure an existing `.env`, run:

```sh
python3 tools/configure_env.py --force
```

```sh
make unbound
```

The first run asks for local server details and writes them to `.env`. `REPORT_ADDRESS` is how clients can reach the server, and `REPORT_NAME` is the server title shown to players.

To run it as a background daemon instead:

```sh
make unbound@daemon
```
