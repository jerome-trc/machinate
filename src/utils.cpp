/**
 * @file utils.cpp
 * @brief Utility functionality and static definitions.
 */

#include "file.hpp"
#include "log.hpp"
#include "string.hpp"
#include "time.hpp"

#include <SDL2/SDL.h>
#include <mutex>

namespace stdfs = std::filesystem;

static std::string get_base_path() noexcept
{
	// TODO: This may be a good place to check install integrity,
	// if not in a launcher of some sort
	char* path = SDL_GetBasePath();
	std::string ret = path;
	SDL_free(path);
	return ret;
}

std::string mxn::get_userdata_path(const std::string& appname) noexcept
{
	char* p = SDL_GetPrefPath("RatCircus", appname.c_str());
	std::string ret = p;
	SDL_free(p);
	return ret;
}

const std::string mxn::base_path = get_base_path();
const std::string mxn::user_path = mxn::get_userdata_path(MXN_USERPATH);

void mxn::vfs_init(const std::string& argv0)
{
	assert(PHYSFS_isInit() == 0);

	if (PHYSFS_init(argv0.c_str()) == 0)
	{
		throw std::runtime_error(fmt::format(
			"PhysicsFS failed to properly initialise: {}",
			PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode())));
	}
}

void mxn::vfs_deinit()
{
	assert(PHYSFS_isInit() != 0);

	if (PHYSFS_deinit() == 0)
		MXN_WARNF(
			"PhysicsFS failed to properly deinitialise: {}",
			PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
}

void mxn::vfs_mount(const stdfs::path& path, const stdfs::path& mount_point)
{
	if (!stdfs::exists(path))
	{ MXN_ERRF("Attempted to mount non-existent path: {}", path.string()); }

	if (PHYSFS_mount(path.c_str(), mount_point.c_str(), 1) == 0)
	{
		MXN_ERRF(
			"Failed to mount {} as \"{}\":\n\t{}", path.string(), mount_point.string(),
			PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	else
		MXN_LOGF("Mounted {} as \"{}\"", path.string(), mount_point.string());
}

bool mxn::vfs_exists(const stdfs::path& path) noexcept
{
	return PHYSFS_exists(path.c_str()) != 0;
}

bool mxn::vfs_isdir(const stdfs::path& path) noexcept
{
	PHYSFS_Stat stat = {};

	if (PHYSFS_stat(path.c_str(), &stat) == 0)
	{
		MXN_ERRF("Requested directory status of invalid file: {}", path.string());
		return false;
	}

	return stat.filetype == PHYSFS_FILETYPE_DIRECTORY;
}

uint32_t mxn::vfs_count(const stdfs::path& path) noexcept
{
	if (!vfs_exists(path)) return 0;

	char** files = PHYSFS_enumerateFiles(path.empty() ? "/" : path.c_str());
	uint32_t ret = 0;

	for (char** f = files; *f != nullptr; f++) ret++;

	PHYSFS_freeList(files);
	return ret;
}

void mxn::vfs_read(const stdfs::path& path, std::vector<unsigned char>& buffer)
{
	assert(buffer.empty());

	if (!vfs_exists(path))
	{
		MXN_ERRF("Attempted to read file from non-existent path: {}", path.string());
		return;
	}

	if (vfs_isdir(path))
	{
		MXN_ERRF("Illegal attempt to read directory: {}", path.string());
		return;
	}

	PHYSFS_File* pfs = PHYSFS_openRead(path.c_str());
	if (pfs == nullptr)
	{
		MXN_ERRF(
			"Failed to open file for read: {}\n\t{}", path.string(),
			PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
		return;
	}

	const PHYSFS_sint64 len = PHYSFS_fileLength(pfs);

	if (len <= -1)
	{
		MXN_ERRF(
			"Failed to determine file length: {}\n\t{}", path.string(),
			PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
		return;
	}

	buffer.resize(static_cast<size_t>(len));
	const PHYSFS_sint64 read = PHYSFS_readBytes(pfs, buffer.data(), len);

	if (read <= -1)
	{
		MXN_ERRF(
			"Error while reading file: {}\n\t{}", path.string(),
			PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
		return;
	}

	if (static_cast<size_t>(read) < buffer.size() && PHYSFS_eof(pfs) == 0)
	{
		MXN_ERRF(
			"Incomplete read of file: {}\n\t{}", path.string(),
			PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
		return;
	}

	if (PHYSFS_close(pfs) == 0)
	{
		MXN_ERRF(
			"Failed to close virtual file handle: {}\n\t{}", path.string(),
			PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
}

void mxn::vfs_recur(const stdfs::path& path, void* userdata, vfs_enumerator func)
{
	if (PHYSFS_enumerate(path.c_str(), func, userdata) == 0)
	{
		MXN_ERRF(
			"VFS recursion failed: {}", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
}

void mxn::ccmd_file(const std::string& path)
{
	if (!vfs_exists(stdfs::path(path)))
	{
		MXN_LOGF("Non-existent path: {}", path);
		return;
	}

	char** files = PHYSFS_enumerateFiles(path.c_str());
	MXN_LOGF("Files under \"{}\" ({}):", path, vfs_count(stdfs::path(path)));

	for (char** f = files; *f != nullptr; f++) { MXN_LOGF("\t{}", std::string(*f)); }

	PHYSFS_freeList(files);
}

const std::chrono::system_clock::time_point mxn::start_time =
	std::chrono::system_clock::now();

quill::Handler* mxn::quillhandler_file = nullptr;
quill::Handler* mxn::quillhandler_stdout = nullptr;
quill::Handler* mxn::quillhandler_history = nullptr;

quill::Logger* mxn::qlog = nullptr;

void mxn::log_init()
{
#ifdef _WIN32
	quill::init_signal_handler();
#endif

	quill::enable_console_colours();
	quill::start(true);
	quill::preallocate();

	quillhandler_stdout = quill::stdout_handler();
	quillhandler_stdout->set_pattern(
		QUILL_STRING("%(ascii_time) [%(thread)] %(filename):%(lineno) "
					 "%(level_name): %(message)"),
		"%H:%M:%S");
	quillhandler_history = quill::create_handler<log_history_handler>("history");
	quillhandler_history->set_pattern(QUILL_STRING("%(level_name): %(message)"));

	qlog =
		quill::create_logger("mxn_logger", { quillhandler_stdout, quillhandler_history });

	qlog->set_log_level(quill::LogLevel::Debug);
}

bool streq(const char* const s1, const char* const s2) { return strcmp(s1, s2) == 0; }

std::vector<std::string> str_split(
	const std::string& string, const std::string& delimiter)
{
	size_t next = 0, last = 0;
	std::string token;
	std::vector<std::string> tokens;
	while ((next = string.find(delimiter, last)) != std::string::npos)
	{
		token = string.substr(last, next - last);
		last = next + 1;
		tokens.push_back(token);
	}
	tokens.push_back(string.substr(last));
	return tokens;
}

std::vector<std::string> str_split(const std::string& string, const char delimiter)
{
	size_t next = 0, last = 0;
	std::string token;
	std::vector<std::string> tokens;
	while ((next = string.find(delimiter, last)) != std::string::npos)
	{
		token = string.substr(last, next - last);
		last = next + 1;
		tokens.push_back(token);
	}
	tokens.push_back(string.substr(last));
	return tokens;
}

std::string str_tolower(const std::string& string)
{
	std::string ret = string;
	std::transform(
		ret.begin(), ret.end(), ret.begin(),
		[](unsigned char c) -> unsigned char { return std::tolower(c); });
	return ret;
}

static std::mutex mtx_localtime;

const tm* localtime_ts(const time_t* time) noexcept
{
	std::scoped_lock<std::mutex> lock(mtx_localtime);
	return std::localtime(time);
}
