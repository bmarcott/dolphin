#include "SlippiReplayComm.h"
#include <cctype>
#include <memory>
#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Logging/LogManager.h"
#include "Core/ConfigManager.h"

std::unique_ptr<SlippiReplayComm> g_replayComm;

// https://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring
// trim from start (in place)
static inline void ltrim(std::string& s)
{
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
}

// trim from end (in place)
static inline void rtrim(std::string& s)
{
  s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(),
          s.end());
}

// trim from both ends (in place)
static inline void trim(std::string& s)
{
  ltrim(s);
  rtrim(s);
}

SlippiReplayComm::SlippiReplayComm()
{
  INFO_LOG(EXPANSIONINTERFACE, "SlippiReplayComm: Using playback config path: %s",
           SConfig::GetInstance().m_strSlippiInput.c_str());
  m_config_file_path = SConfig::GetInstance().m_strSlippiInput.c_str();
}

SlippiReplayComm::~SlippiReplayComm()
{
}

SlippiReplayComm::CommSettings SlippiReplayComm::GetSettings()
{
  return m_comm_file_settings;
}

std::string SlippiReplayComm::GetReplayPath()
{
  std::string replay_file_path = m_comm_file_settings.replayPath;
  if (m_comm_file_settings.mode == "queue")
  {
    // If we are in queue mode, let's grab the replay from the queue instead
    replay_file_path = m_comm_file_settings.queue.empty() ? "" : m_comm_file_settings.queue.front().path;
  }

  return replay_file_path;
}

bool SlippiReplayComm::IsNewReplay()
{
  LoadFile();
  std::string replay_file_path = GetReplayPath();

  bool has_path_changed = replay_file_path != m_previous_replay_loaded;
  bool is_replay = !!replay_file_path.length();

  // The previous check is mostly good enough but it does not
  // work if someone tries to load the same replay twice in a row
  // the commandId was added to deal with this
  bool has_command_changed = m_comm_file_settings.commandId != m_previous_command_id;

  // This checks if the queue index has changed, this is to fix the
  // issue where the same replay showing up twice in a row in a
  // queue would never cause this function to return true
  bool has_queue_idx_changed = false;
  if (m_comm_file_settings.mode == "queue" && !m_comm_file_settings.queue.empty())
  {
    has_queue_idx_changed = m_comm_file_settings.queue.front().index != m_previous_index;
  }

  bool is_new_replay = has_path_changed || has_command_changed || has_queue_idx_changed;

  return is_replay && is_new_replay;
}

void SlippiReplayComm::NextReplay()
{
  if (m_comm_file_settings.queue.empty())
    return;

  // Increment queue position
  m_comm_file_settings.queue.pop();
}

std::unique_ptr<Slippi::SlippiGame> SlippiReplayComm::LoadGame()
{
  auto replay_file_path = GetReplayPath();
  INFO_LOG(EXPANSIONINTERFACE, "Attempting to load replay file %s", replay_file_path.c_str());
  auto result = Slippi::SlippiGame::FromFile(replay_file_path);
  if (result)
  {
    // If we successfully loaded a SlippiGame, indicate as such so
    // that this game won't be considered new anymore. If the replay
    // file did not exist yet, result will be falsy, which will keep
    // the replay considered new so that the file will attempt to be
    // loaded again
    m_previous_replay_loaded = replay_file_path;
    m_previous_command_id = m_comm_file_settings.commandId;
    if (m_comm_file_settings.mode == "queue" && !m_comm_file_settings.queue.empty())
    {
      m_previous_index = m_comm_file_settings.queue.front().index;
    }

    WatchSettings ws;
    ws.path = replay_file_path;
    ws.startFrame = m_comm_file_settings.startFrame;
    ws.endFrame = m_comm_file_settings.endFrame;
    if (m_comm_file_settings.mode == "queue")
    {
      ws = m_comm_file_settings.queue.front();
    }

    if (m_comm_file_settings.outputOverlayFiles)
    {
      std::string dir_path = File::GetExeDirectory();
      File::WriteStringToFile(ws.gameStation, dir_path + DIR_SEP + "Slippi/out-station.txt");
      File::WriteStringToFile(ws.gameStartAt, dir_path + DIR_SEP + "Slippi/out-time.txt");
    }

    m_current = ws;
  }

  return std::move(result);
}

void SlippiReplayComm::LoadFile()
{
  // TODO: Consider even only checking file mod time every 250 ms or something? Not sure
  // TODO: what the perf impact is atm

  u64 mod_time = File::GetFileModTime(m_config_file_path);
  if (mod_time != 0 && mod_time == m_config_last_load_mod_time)
  {
    // TODO: Maybe be smarter than just using mod time? Look for other things that would
    // TODO: indicate that file has changed and needs to be reloaded?
    return;
  }

  WARN_LOG(EXPANSIONINTERFACE, "File change detected in comm file: %s", m_config_file_path.c_str());
  m_config_last_load_mod_time = mod_time;

  // TODO: Maybe load file in a more intelligent way to save
  // TODO: file operations
  std::string comm_file_contents;
  File::ReadFileToString(m_config_file_path, comm_file_contents);

  auto res = json::parse(comm_file_contents, nullptr, false);
  if (res.is_discarded() || !res.is_object())
  {
    // Happens if there is a parse error, I think?
    m_comm_file_settings.mode = "normal";
    m_comm_file_settings.replayPath = "";
    m_comm_file_settings.startFrame = Slippi::GAME_FIRST_FRAME;
    m_comm_file_settings.endFrame = INT_MAX;
    m_comm_file_settings.commandId = "";
    m_comm_file_settings.outputOverlayFiles = false;
    m_comm_file_settings.isRealTimeMode = false;
    m_comm_file_settings.rollbackDisplayMethod = "off";

    if (res.is_string())
    {
      // If we have a string, let's use that as the replayPath
      // This is really only here because when developing it might be easier
      // to just throw in a string instead of an object

      m_comm_file_settings.replayPath = res.get<std::string>();
    }
    else
    {
      WARN_LOG(EXPANSIONINTERFACE, "Comm file load error detected. Check file format");

      // Reset in the case of read error. this fixes a race condition where file mod time changes
      // but the file is not readable yet?
      m_config_last_load_mod_time = 0;
    }

    return;
  }

  // TODO: Support file with only path string
  m_comm_file_settings.mode = res.value("mode", "normal");
  m_comm_file_settings.replayPath = res.value("replay", "");
  m_comm_file_settings.startFrame = res.value("startFrame", Slippi::GAME_FIRST_FRAME);
  m_comm_file_settings.endFrame = res.value("endFrame", INT_MAX);
  m_comm_file_settings.commandId = res.value("commandId", "");
  m_comm_file_settings.outputOverlayFiles = res.value("outputOverlayFiles", false);
  m_comm_file_settings.isRealTimeMode = res.value("isRealTimeMode", false);
  m_comm_file_settings.rollbackDisplayMethod = res.value("rollbackDisplayMethod", "off");

  if (m_is_first_load)
  {
    auto queue = res["queue"];
    if (queue.is_array())
    {
      int index = 0;
      for (json::iterator it = queue.begin(); it != queue.end(); ++it)
      {
        json el = *it;
        WatchSettings w = {};
        w.path = el.value("path", "");
        w.startFrame = el.value("startFrame", Slippi::GAME_FIRST_FRAME);
        w.endFrame = el.value("endFrame", INT_MAX);
        w.gameStartAt = el.value("gameStartAt", "");
        w.gameStation = el.value("gameStation", "");
        w.index = index++;

        m_comm_file_settings.queue.push(w);
      };
    }

    m_is_first_load = false;
  }
}
