/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.

The VOX File format is Copyright to their respectful owners.

 */
#pragma once
#ifndef VOX_BINARY_H
#define VOX_BINARY_H

#include "IsoVoxel.h"
#include "voxelModel.h"

#include <string_view>
#include <filesystem>

#define VOX_FILE_EXT L".vox"
#define V1X_FILE_EXT L".v1x"

namespace Volumetric
{
namespace voxB
{
	// see voxelModel.h

// builds the voxel model, loading from magickavoxel .vox format, returning the model with the voxel traversal
// *** will save a cached version of culled model if it doesn't exist
// *** will load cached "culled" version if newer than matching .vox to speedify loading voxel models
bool const LoadVOX(std::filesystem::path const path, voxelModelBase* const __restrict pDestMem, bool const stacked = false);
bool const AddEmissiveVOX(std::wstring_view const file_no_extension, voxelModelBase* const __restrict pOwner, bool const stacked = false);
bool const AddVideoscreenVOX(std::wstring_view const file_no_extension, voxelModelBase* const __restrict pOwner, bool const stacked = false);
bool const AddTransparentVOX(std::wstring_view const file_no_extension, voxelModelBase* const __restrict pOwner, bool const stacked = false);

void ApplyAllTransparent(voxelModelBase* const __restrict pModel);  // default transparency level for model instances is 0.5f(DEFAULT_TRANSPARENCY), each instance has independent transparency
	

} // end namespace voxB

// ## forward declarations first

} // end namespace



#endif // VOX_BINARY_H

