/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

//
// Sprite caching system.
//
// SpriteFile handles sprite serialization and streaming.
// SpriteCache provides bitmaps by demand; it uses SpriteFile to load sprites
// and does MRU (most-recent-use) caching.
//
// TODO: store sprite data in a specialized container type that is optimized
// for having most keys allocated in large continious sequences by default.
//
// Only for the reference: one of the ideas is for container to have a table
// of arrays of fixed size internally. When getting an item the hash would be
// first divided on array size to find the array the item resides in, then the
// item is taken from item from slot index = (hash - arrsize * arrindex).
// TODO: find out if there is already a hash table kind that follows similar
// principle.
//
//=============================================================================

#ifndef AGS_SHARED_AC_SPRITE_CACHE_H
#define AGS_SHARED_AC_SPRITE_CACHE_H

#include "ags/lib/std/memory.h"
#include "ags/lib/std/vector.h"
#include "ags/shared/core/platform.h"
#include "ags/shared/util/error.h"
#include "ags/shared/util/geometry.h"

namespace AGS3 {

namespace AGS {
namespace Shared {
class String;
class Stream;
class Bitmap;
} // namespace AGS3
} // namespace AGS

using namespace AGS; // FIXME later
typedef AGS::Shared::HError HAGSError;

struct SpriteInfo;

// Tells that the sprite is found in the game resources.
#define SPRCACHEFLAG_ISASSET        0x01
// Tells that the sprite index was remapped to another existing sprite.
#define SPRCACHEFLAG_REMAPPED       0x02
// Locked sprites are ones that should not be freed when out of cache space.
#define SPRCACHEFLAG_LOCKED         0x04

// Max size of the sprite cache, in bytes
#if AGS_PLATFORM_OS_ANDROID || AGS_PLATFORM_OS_IOS
#define DEFAULTCACHESIZE_KB (32 * 1024)
#else
#define DEFAULTCACHESIZE_KB (128 * 1024)
#endif

// TODO: research old version differences
enum SpriteFileVersion {
	kSprfVersion_Uncompressed = 4,
	kSprfVersion_Compressed = 5,
	kSprfVersion_Last32bit = 6,
	kSprfVersion_64bit = 10,
	kSprfVersion_HighSpriteLimit = 11,
	kSprfVersion_Current = kSprfVersion_HighSpriteLimit
};

enum SpriteIndexFileVersion {
	kSpridxfVersion_Initial = 1,
	kSpridxfVersion_Last32bit = 2,
	kSpridxfVersion_64bit = 10,
	kSpridxfVersion_HighSpriteLimit = 11,
	kSpridxfVersion_Current = kSpridxfVersion_HighSpriteLimit
};


typedef int32_t sprkey_t;

// SpriteFileIndex contains sprite file's table of contents
struct SpriteFileIndex {
	int SpriteFileIDCheck = 0; // tag matching sprite file and index file
	sprkey_t LastSlot = -1;
	size_t SpriteCount = 0u;
	std::vector<int16_t> Widths;
	std::vector<int16_t> Heights;
	std::vector<soff_t>  Offsets;
};

class SpriteFile {
public:
	// Standart sprite file and sprite index names
	static const char *const DefaultSpriteFileName;
	static const char *const DefaultSpriteIndexName;

	SpriteFile();
	// Loads sprite reference information and inits sprite stream
	HAGSError   OpenFile(const Shared::String &filename, const Shared::String &sprindex_filename,
		std::vector<Size> &metrics);
	void        Reset();

	// Tells if bitmaps in the file are compressed
	bool        IsFileCompressed() const;
	// Tells the highest known sprite index
	sprkey_t    GetTopmostSprite() const;

	// Loads sprite index file
	bool        LoadSpriteIndexFile(const Shared::String &filename, int expectedFileID,
		soff_t spr_initial_offs, sprkey_t topmost, std::vector<Size> &metrics);
	// Rebuilds sprite index from the main sprite file
	HAGSError   RebuildSpriteIndex(AGS::Shared::Stream *in, sprkey_t topmost, SpriteFileVersion vers,
		std::vector<Size> &metrics);

	HAGSError LoadSprite(sprkey_t index, Shared::Bitmap *&sprite);
	HAGSError LoadSpriteData(sprkey_t index, Size &metric, int &bpp, std::vector<char> &data);

	// Saves all sprites to file; fills in index data for external use
	// TODO: refactor to be able to save main file and index file separately (separate function for gather data?)
	static int  SaveToFile(const Shared::String &save_to_file,
		const std::vector<Shared::Bitmap *> &sprites, // available sprites (may contain nullptrs)
		SpriteFile *read_from_file, // optional file to read missing sprites from
		bool compressOutput, SpriteFileIndex &index);
	// Saves sprite index table in a separate file
	static int  SaveSpriteIndex(const Shared::String &filename, const SpriteFileIndex &index);

private:
	// Finds the topmost occupied slot index. Warning: may be slow.
	static sprkey_t FindTopmostSprite(const std::vector<Shared::Bitmap *> &sprites);
	// Seek stream to sprite
	void        SeekToSprite(sprkey_t index);

	// Internal sprite reference
	struct SpriteRef {
		soff_t Offset = 0; // data offset
		size_t Size = 0;   // cache size of element, in bytes
	};

	// Array of sprite references
	std::vector<SpriteRef> _spriteData;
	std::unique_ptr<Shared::Stream> _stream; // the sprite stream
	bool _compressed; // are sprites compressed
	sprkey_t _curPos; // current stream position (sprite slot)
};

class SpriteCache {
public:
	static const sprkey_t MIN_SPRITE_INDEX = 1; // 0 is reserved for "empty sprite"
	static const sprkey_t MAX_SPRITE_INDEX = INT32_MAX - 1;
	static const size_t   MAX_SPRITE_SLOTS = INT32_MAX;

	SpriteCache(std::vector<SpriteInfo> &sprInfos);
	~SpriteCache();

	// Loads sprite reference information and inits sprite stream
	HAGSError   InitFile(const Shared::String &filename, const Shared::String &sprindex_filename);
	// Saves current cache contents to the file
	int         SaveToFile(const Shared::String &filename, bool compressOutput, SpriteFileIndex &index);
	// Closes an active sprite file stream
	void        DetachFile();

	inline bool IsFileCompressed() const {
		return _file.IsFileCompressed();
	}

	// Tells if there is a sprite registered for the given index;
	// this includes sprites that were explicitly assigned but failed to init and were remapped
	bool        DoesSpriteExist(sprkey_t index) const;
	// Makes sure sprite cache has allocated slots for all sprites up to the given inclusive limit;
	// returns requested index on success, or -1 on failure.
	sprkey_t    EnlargeTo(sprkey_t topmost);
	// Finds a free slot index, if all slots are occupied enlarges sprite bank; returns index
	sprkey_t    GetFreeIndex();
	// Returns current size of the cache, in bytes
	size_t      GetCacheSize() const;
	// Gets the total size of the locked sprites, in bytes
	size_t      GetLockedSize() const;
	// Returns maximal size limit of the cache, in bytes
	size_t      GetMaxCacheSize() const;
	// Returns number of sprite slots in the bank (this includes both actual sprites and free slots)
	size_t      GetSpriteSlotCount() const;
	// Loads sprite and and locks in memory (so it cannot get removed implicitly)
	void        Precache(sprkey_t index);
	// Remap the given index to the sprite 0
	void        RemapSpriteToSprite0(sprkey_t index);
	// Unregisters sprite from the bank and optionally deletes bitmap
	void        RemoveSprite(sprkey_t index, bool freeMemory);
	// Deletes all loaded (non-locked, non-external) images from the cache;
	// this keeps all the auxiliary sprite information intact
	void        DisposeAll();
	// Deletes all data and resets cache to the clear state
	void        Reset();
	// Assigns new sprite for the given index; this sprite won't be auto disposed
	void        SetSprite(sprkey_t index, Shared::Bitmap *);
	// Assigns new sprite for the given index, remapping it to sprite 0;
	// optionally marks it as an asset placeholder
	void        SetEmptySprite(sprkey_t index, bool as_asset);
	// Assigns new bitmap for the *registered* sprite without changing its properties
	void        SubstituteBitmap(sprkey_t index, Shared::Bitmap *);
	// Sets max cache size in bytes
	void        SetMaxCacheSize(size_t size);

	// Loads (if it's not in cache yet) and returns bitmap by the sprite index
	Shared::Bitmap *operator[] (sprkey_t index);

private:
	void        Init();
	// Load sprite from game resource
	size_t      LoadSprite(sprkey_t index);
	// Gets the index of a sprite which data is used for the given slot;
	// in case of remapped sprite this will return the one given sprite is remapped to
	sprkey_t    GetDataIndex(sprkey_t index);
	// Delete the oldest image in cache
	void        DisposeOldest();

	// Information required for the sprite streaming
	// TODO: split into sprite cache and sprite stream data
	struct SpriteData {
		size_t          Size; // to track cache size
		uint32_t        Flags;
		// TODO: investigate if we may safely use unique_ptr here
		// (some of these bitmaps may be assigned from outside of the cache)
		Shared::Bitmap *Image; // actual bitmap

		// Tells if there actually is a registered sprite in this slot
		bool DoesSpriteExist() const;
		// Tells if there's a game resource corresponding to this slot
		bool IsAssetSprite() const;
		// Tells if sprite was added externally, not loaded from game resources
		bool IsExternalSprite() const;
		// Tells if sprite is locked and should not be disposed by cache logic
		bool IsLocked() const;

		SpriteData();
		~SpriteData();
	};

	// Provided map of sprite infos, to fill in loaded sprite properties
	std::vector<SpriteInfo> &_sprInfos;
	// Array of sprite references
	std::vector<SpriteData> _spriteData;

	SpriteFile _file;

	size_t _maxCacheSize;  // cache size limit
	size_t _lockedSize;    // size in bytes of currently locked images
	size_t _cacheSize;     // size in bytes of currently cached images

	// MRU list: the way to track which sprites were used recently.
	// When clearing up space for new sprites, cache first deletes the sprites
	// that were last time used long ago.
	std::vector<sprkey_t> _mrulist;
	std::vector<sprkey_t> _mrubacklink;
	int _liststart;
	int _listend;

	// Initialize the empty sprite slot
	void        InitNullSpriteParams(sprkey_t index);
};

} // namespace AGS3

#endif
