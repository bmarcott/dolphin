#pragma once

#include <SlippiGame.h>
#include <limits.h>
#include <nlohmann/json.hpp>
#include <queue>
#include <string>

#include <Common/CommonTypes.h>

using json = nlohmann::json;

class SlippiReplayComm
{
public:
  typedef struct WatchSettings
  {
    std::string path;
    int startFrame = Slippi::GAME_FIRST_FRAME;
    int endFrame = INT_MAX;
    std::string gameStartAt = "";
    std::string gameStation = "";
    int index = 0;
  } WatchSettings;

  // Loaded file contents
  typedef struct CommSettings
  {
    std::string mode;
    std::string replayPath;
    int startFrame = Slippi::GAME_FIRST_FRAME;
    int endFrame = INT_MAX;
    bool outputOverlayFiles;
    bool isRealTimeMode;
    std::string rollbackDisplayMethod;  // off, normal, visible
    std::string commandId;
    std::queue<WatchSettings> queue;
  } CommSettings;

  SlippiReplayComm();
  ~SlippiReplayComm();

  CommSettings GetSettings();
  void NextReplay();
  bool IsNewReplay();
  std::unique_ptr<Slippi::SlippiGame> LoadGame();

  WatchSettings m_current;

private:
  void LoadFile();
  std::string GetReplayPath();

  std::string m_config_file_path;
  json m_file_data;
  std::string m_previous_replay_loaded;
  std::string m_previous_command_id;
  int m_previous_index;

  u64 m_config_last_load_mod_time;

  // Queue stuff
  bool m_is_first_load = true;
  bool m_provide_new = false;
  int m_queue_pos = 0;

  CommSettings m_comm_file_settings;
};
