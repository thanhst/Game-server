# Current Architecture Diagram

## Text-Based Architecture Diagram

```text
HUNR Java Server

  Spring Boot
  +-- Run.main()
      +-- Spring repositories
      +-- GameRepository singleton
      +-- DropRateService.load()
      +-- BoMongService.loadConfig()
      +-- MatrixChallengePC.loadPCKey()
      +-- DragonBall.start()
          +-- Server()
              +-- Config.load(application.properties)
          +-- Server.init()
              +-- AutoBackup
              +-- MySQLConnect single JDBC connection
              +-- SQLStatement table loaders
              +-- Content caches: item/map/skill/part/effect/image/etc.
              +-- MainConfig.load(Config/config.ini)
              +-- MapManager/TMap/Zone construction
              +-- Clan/Top/Lucky/Consignment/Event setup
          +-- Server.start()
              +-- Game ServerSocket
              +-- VoiceServer
              +-- MainUpdate thread
              +-- AutoSaveData thread
              +-- BossManager
              +-- MapManager thread
              +-- accept loop
                  +-- Session
                      +-- MessageCollector thread
                      +-- Sender thread
                      +-- Heartbeat task
                      +-- MessageHandler
                      +-- Service
                      +-- User/Player
                          +-- Zone
                          +-- MapManager
                          +-- GameRepository
                          +-- MySQLConnect
```

```text
C++ Target

  main.cpp
  +-- EnvLoader
  +-- MySqlContentLoader
      +-- ContentRegistry
          +-- maps, mobs, NPCs, skills, items, tasks, config, boss spawns
  +-- IGameRepository
      +-- MySqlGameRepository or InMemoryGameRepository
  +-- MySqlAutoBackupService
  +-- WorldService
      +-- PlayerService
      +-- InventoryService
      +-- Scheduler/EventScheduler
      +-- BossManager
      +-- map_zones_
  +-- GatewayServer
      +-- CommandDispatcher
      +-- text or legacy binary client protocol
      +-- WorldService.Execute(...)
```

## Subsystem Dependency Graph

```text
Run
  -> GameRepository
  -> DropRateService
  -> BoMongService
  -> MatrixChallengePC
  -> DragonBall

DragonBall
  -> Server

Server
  -> Config
  -> MySQLConnect
  -> SQLStatement
  -> AutoBackup
  -> AutoSaveData
  -> MainUpdate
  -> SessionManager
  -> VoiceServer
  -> MapManager
  -> TMap
  -> Zone
  -> BossManager
  -> Top
  -> Lucky
  -> Consignment
  -> ClanManager
  -> Card/CrackBall/RandomItem/Skills

Config
  -> application.properties resource

MainConfig
  -> Config/config.ini

MySQLConnect
  -> one static JDBC Connection

GameRepository
  -> Spring Data repository beans

Session
  -> Socket
  -> MessageHandler
  -> Service
  -> User
  -> Player
  -> SessionManager
  -> MatrixChallengeManager

MessageHandler
  -> Service
  -> Player
  -> VoiceMessageService/VoiceChatManager
  -> GameRepository
  -> event/boss services

Service
  -> Session
  -> Player
  -> DragonBall.Server caches
  -> Zone/MapService

Player
  -> Service
  -> Session/User
  -> Zone
  -> MapManager
  -> GameRepository
  -> MySQLConnect through helper paths
  -> Item/Skill/Task/Clan/Combine/Event systems

TMap
  -> resources/map/*
  -> resources/map/block/*
  -> Zone subclasses

Zone
  -> TMap
  -> MapService
  -> Player
  -> Mob
  -> ItemMap
  -> Npc
```

## Java Runtime Flow Diagram

```text
Process Start
  |
  v
Spring Boot context
  |
  v
Run.run()
  |
  +--> wire repositories into GameRepository singleton
  +--> load DropRate/BoMong/Security config
  |
  v
DragonBall.start()
  |
  v
Server constructor
  |
  +--> Config.load(application.properties)
  |
  v
Server.init()
  |
  +--> MySQLConnect.create(...)
  +--> load static content tables
  +--> parse DB JSON fields
  +--> load resources/map files
  +--> build caches
  +--> create maps/zones
  +--> initialize game/event services
  |
  v
Server.start()
  |
  +--> start voice server
  +--> start autosave thread
  +--> start event update thread
  +--> start map manager thread
  +--> spawn/open initial world events
  |
  v
Accept loop
  |
  v
Session created
  |
  +--> MessageCollector reads command
  +--> MessageHandler dispatches command
  +--> Player/Service/Zone mutate state and send replies
  |
  v
Zone threads update world every 100 ms
```

## Client Command Flow Diagram

```text
Client socket
  |
  v
Session.readMessage()
  |
  +-- command == GET_SESSION_ID
  |     -> generate/send XOR key
  |
  +-- command != GET_SESSION_ID
        -> MessageHandler.onMessage()
              |
              +-- NOT_LOGIN
              |     -> CLIENT_INFO
              |     -> MATRIX_CHALLENGE
              |     -> LOGIN/REGISTER
              |
              +-- NOT_MAP
              |     -> resource/map/item updates
              |     -> create player
              |
              +-- map/runtime command
                    -> Player method or Service method
                    -> Zone/Map/Repository mutation
                    -> Service serializes response Message
                    -> Session.Sender writes socket
```

## Data Loading Diagram

```text
application.properties
  -> Config
  -> Server port/db/version/runtime settings

Config/config.ini
  -> MainConfig
  -> rates and combine/cold reward values

sql/bo_mong_setup.sql
  -> Bo Mong schema source only
  -> BoMongService repositories at runtime

MySQL content tables
  -> SQLStatement
  -> Server.init* methods
      -> in-memory templates and caches
      -> TMap definitions
      -> Mob/Npc/Skill/Item/Task/etc.

resources/map/*
  -> TMap.loadMapFromResource()
  -> tile/block/collision runtime data
```

## C++ Target Runtime Flow Diagram

```text
main.cpp
  |
  +--> EnvLoader::LoadAppConfig(.env)
  |
  +--> MySqlContentLoader.Connect()
  |     -> LoadAllContent()
  |        -> ContentRegistry
  |
  +--> MySqlAutoBackupService.Start()
  |
  +--> choose repository
  |     -> MySqlGameRepository when storage=mysql and connection succeeds
  |     -> InMemoryGameRepository otherwise, unless storage is required
  |
  +--> WorldService(repository, registry, tick_ms=50)
  |     -> Start()
  |
  +--> GatewayServer(world_service, port)
        -> Start()
        -> accept clients
        -> dispatch commands into WorldService
```

