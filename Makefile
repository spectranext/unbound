all:

configure:
	python3 tools/configure_env.py

unbound@build: configure
	docker build -t spectranext/unbound:latest .

unbound: unbound@build
	docker run --rm --env-file .env -p 13390:13390 spectranext/unbound:latest

unbound@daemon: unbound@build
	-docker rm -f unbound
	docker run -d --restart=always --name unbound --env-file .env -p 13390:13390 spectranext/unbound:latest

server-api:
	docker compose -f server-api.yml up --build

server-api@up:
	docker compose -f server-api.yml up --build -d

server-api@down:
	docker compose -f server-api.yml down

.PHONY: unbound
.PHONY: configure
.PHONY: unbound@build
.PHONY: unbound@daemon
.PHONY: server-api
.PHONY: server-api@up
.PHONY: server-api@down
