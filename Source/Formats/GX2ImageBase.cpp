#include "GX2ImageBase.h"

#include "DDS.h"

void GX2ImageBase::SetupInfoStructs()
{
  // Set up the info structs from the format and tile mode.
  // Default to GX2_SURFACE_FORMAT_INVALID.
  m_format_info = m_format_info_list[0];
  for (int i = 0; i < m_format_info_list.size(); i++)
  {
    if (m_base_header->format == static_cast<quint32>(m_format_info_list[i].id))
    {
      m_format_info = m_format_info_list[i];
      m_format_info_index = i;
      break;
    }
  }

  m_format_info.shared_info = m_shared_format_info_list[0];
  for (int i = 0; i < m_shared_format_info_list.size(); i++)
  {
    if (m_format_info.format == m_shared_format_info_list[i].format)
    {
      m_format_info.shared_info = m_shared_format_info_list[i];
      break;
    }
  }

  // Default to a dummy value.
  m_tile_mode_info = m_tile_mode_info_list[m_base_header->tile_mode];
  for (int i = 0; i < m_shared_tile_mode_info_list.size(); i++)
  {
    if (m_tile_mode_info.mode == m_shared_tile_mode_info_list[i].mode)
    {
      m_tile_mode_info.shared_info = m_shared_tile_mode_info_list[i];
      break;
    }
  }
}

ResultCode GX2ImageBase::ReadImageFromData()
{
  return CopyImage(m_raw_image_data, m_deswizzled_image_data, false);
}

ResultCode GX2ImageBase::WriteDeswizzledImageToData()
{
  return CopyImage(m_deswizzled_image_data, m_raw_image_data, true);
}

ResultCode GX2ImageBase::ImportDDS(QString path)
{
  DDS dds;
  dds.SetPath(path);
  dds.ReadFile();
  m_deswizzled_image_data = dds.GetImageData();
  // TODO: Padding.
  return WriteDeswizzledImageToData();
}

ResultCode GX2ImageBase::ExportToDDS(QString path)
{
  DDS dds;
  dds.SetPath(path);
  dds.SetImageData(m_deswizzled_image_data);
  int bytes_written =
      dds.WriteFile(m_base_header->width, m_base_header->height, m_base_header->depth,
                    m_base_header->num_mips, true, m_format_info);
  if (bytes_written == 0)
    return ResultCode::RESULT_NO_BYTES_WRITTEN;
  else
    return ResultCode::RESULT_SUCCESS;
}

ResultCode GX2ImageBase::CopyImage(QByteArray* source, QByteArray*& destination, bool swizzle)
{
  // Set up dimensions.
  quint32 width = m_base_header->width;
  quint32 height = m_base_header->height;

  // Temporary hack to find special textures.
  if (m_base_header->aa_mode != 0 || static_cast<quint32>(m_tile_mode_info.thickness) > 1 ||
      m_format_info.name == "GX2_SURFACE_FORMAT_INVALID" ||
      m_format_info.shared_info.format == Format::Invalid)
    return RESULT_UNSUPPORTED_FILE_FORMAT_IMPORTANT;

  m_num_samples = 1 << m_base_header->aa_mode;

  m_pipe_swizzle = Bit(m_base_header->swizzle, 8);
  m_bank_swizzle = GetBits(m_base_header->swizzle, 9, 3);

  if (m_format_info.shared_info.use == SharedFormatInfo::Use::DepthBuffer)
    m_has_depth = true;
  else
    m_has_depth = false;

  if (m_format_info.shared_info.compressed)
  {
    // Split into 4 by 4 compressed blocks of pixels.
    width /= 4;
    height /= 4;
  }

  switch (m_tile_mode_info.mode)
  {
  // Macro Tiled
  // TODO: Add support for 3D textures.
  case TileMode::Macro:
    // Calculate the size of the macro tiles, determined by the number of pipes/banks, and the
    // aspect ratio.

    m_macro_tile_pitch = (m_num_banks * 8) / m_tile_mode_info.aspect_ratio;
    m_macro_tile_height = (m_num_pipes * 8) * m_tile_mode_info.aspect_ratio;

    // Physical width of a row of the whole texture /
    // Physical width of a row in one macro tile =
    // Number of macro tiles in a row of the whole texture.
    m_macro_tiles_per_row = m_base_header->pitch / m_macro_tile_pitch;

    // Physical width of a row in one macro tile *
    // Height of one macro tile =
    // Number of pixels in one macro tile.

    // Number of pixels in one macro tile *
    // Number of bits in one pixel =
    // Number of bits in one sample of a macro tile.

    // Number of bits in one sample of a macro tile. *
    // Thickness of one micro tile =
    // Number of bits in one sample of a macro tile including thickness.

    // Number of bits in one sample of a macro tile including thickness *
    // Number of samples =
    // Number of bits in all samples of a macro tile including thickness.

    // Number of bits in all samples of a macro tile including thickness -> Bytes =
    // Number of bytes in one macro tile (All .
    m_num_macro_tile_bytes =
        BitsToBytes(m_macro_tile_pitch * m_macro_tile_height * m_format_info.shared_info.bpp *
                    static_cast<quint32>(m_tile_mode_info.thickness) * m_num_samples);

    // Physical width of a row of the whole texture *
    // Height of the whole texture =
    // Number of pixels in the whole texture.

    // Number of pixels in the whole texture *
    // Number of bits in one pixel =
    // Number of bits in the whole texture without sampling and thickness.

    // Number of bits in the whole texture without sampling and thickness *
    // Thickness of one micro tile =
    // Number of bits in the whole texture without sampling.

    // Number of bits in the whole texture without sampling *
    // Number of samples =
    // Number of bits in the whole texture.

    // Number of bits in the whole texture -> Bytes =
    // Number of bytes in the whole texture.
    m_num_slice_bytes =
        BitsToBytes(m_base_header->pitch * m_base_header->height * m_format_info.shared_info.bpp *
                    static_cast<quint32>(m_tile_mode_info.thickness) * m_num_samples);
  // Fallthrough
  // (This comment is here to prevent compiler warnings.)

  // Micro Tiled
  case TileMode::Micro:
    if (m_has_depth)
      m_micro_tile_type = MicroTileType::NonDisplayable;
    else
      m_micro_tile_type = MicroTileType::Displayable;

    // Number of pixels in one micro tile *
    // Number of bits in one pixel =
    // Number of bits in one sample of a micro tile.

    // Number of bits in one sample of a micro tile. *
    // Thickness of one micro tile =
    // Number of bits in one sample of a micro tile including thickness.

    // Number of bits in one sample of a micro tile including thickness *
    // Number of samples =
    // Number of bits in all samples of a micro tile including thickness.

    // Number of bits in all samples of a micro tile including thickness -> Bytes =
    // Number of bytes in one micro tile.
    m_num_micro_tile_bits = m_num_micro_tile_pixels * m_format_info.shared_info.bpp *
                            static_cast<quint32>(m_tile_mode_info.thickness) * m_num_samples;

    // Number of bits in one micro tile /
    // Number of bits in a byte =
    // Number of bytes in one micro tile with all samples.
    // TODO: lambda for conversion to bits or something?
    m_num_micro_tile_bytes = m_num_micro_tile_bits / 8;

    // Number of bytes in one micro tile /
    // Number of samples in each micro tile =
    // Number of bytes in one sample of a micro tile.
    m_bytes_per_sample = m_num_micro_tile_bytes / m_num_samples;
    break;

  case TileMode::Linear:
  default:
    return RESULT_UNSUPPORTED_FILE_FORMAT;
  }

  destination = new QByteArray();
  destination->resize(m_base_header->data_length);
  destination->fill(0);

  for (quint32 y = 0; y < height; ++y)
  {
    for (quint32 x = 0; x < width; ++x)
    {
      qint32 original_offset = 0;
      // Get the offset of the pixel at the current coordinate.
      switch (m_tile_mode_info.mode)
      {
      case TileMode::Macro:
        original_offset = ComputeSurfaceAddrFromCoordMacroTiled(x, y, 0, 0, 0, 0);
        break;
      default:
        return RESULT_UNSUPPORTED_FILE_FORMAT;
      }
      quint32 block_size;

      // Write the new pixels in their normal order to the new byte array.
      switch (m_format_info.format)
      {
      case Format::BC1:
      case Format::BC4:
      {
        // todo: this can be outside of the loop
        block_size = 8;
        break;
      }
      default:
        block_size = 16;
        break;
      }
      qint32 new_offset = (y * width + x) * block_size;

      if (original_offset > source->size())
      {
        qDebug("Error: Tried to read block outside of image data. "
               "Skipping pixel.");
        continue;
      }
      if (new_offset > destination->size())
      {
        qDebug() << "Error: Tried to write block outside of image data. "
                    "Skipping pixel.";
        continue;
      }
      if (swizzle)
        destination->replace(original_offset, block_size, source->constData() + new_offset,
                             block_size);
      else
        destination->replace(new_offset, block_size, source->constData() + original_offset,
                             block_size);
    }
  }
  return RESULT_SUCCESS;
}

quint64 GX2ImageBase::ComputeSurfaceAddrFromCoordMacroTiled(quint32 x, quint32 y, quint32 slice,
                                                            quint32 sample, quint32 tile_base,
                                                            quint32 comp_bits)
{
  // The commenting in this function will eventually be removed in favor of a dedicated document
  // explaining the whole thing.

  // TODO: Using m_num_samples directly might be possible. Needs testing with AA surfaces.
  quint32 num_samples = m_num_samples;

  // Get the pixel index within the micro tile.
  quint64 pixel_index_within_micro_tile = ComputePixelIndexWithinMicroTile(x, y, slice);

  // Offset of the beginning of the current sample of the current tile.
  quint64 sample_offset_within_micro_tile;
  // Offset of the current pixel relative to the beginning of the current sample.
  quint64 pixel_offset_within_sample;

  if (m_has_depth)
  {
    if (comp_bits && comp_bits != m_format_info.shared_info.bpp)
    {
      sample_offset_within_micro_tile = tile_base + comp_bits * sample;
      pixel_offset_within_sample = num_samples * comp_bits * pixel_index_within_micro_tile;
    }
    else
    {
      // Number of bits in one pixel *
      // Current sample we're in =
      //
      sample_offset_within_micro_tile = m_format_info.shared_info.bpp * sample;
      pixel_offset_within_sample =
          num_samples * m_format_info.shared_info.bpp * pixel_index_within_micro_tile;
    }
  }
  else
  {
    // Number of bytes in one micro tile with all samples /
    // Number of samples in each micro tile =
    // Number of bits in one micro tile in one sample.

    // Number of bits in one micro tile in one sample *
    // Current sample we're in =
    // Offset of the start of the micro tile sample.

    // This works because I think the structure is like so:
    // Micro Tile 1:
    // Sample 1
    // Sample 2
    // Micro Tile 2:
    // Sample 1
    // Sample 2
    sample_offset_within_micro_tile = (m_num_micro_tile_bits / num_samples) * sample;
    // Number of bits in one pixel *
    // The pixel index =
    // The offset of the current pixel relative to the beginning of the current sample.
    pixel_offset_within_sample = m_format_info.shared_info.bpp * pixel_index_within_micro_tile;
  }

  // Offset of the pixel within the sample +
  // Offset of the beginning of the current sample =
  // Element Offset. (Pixel offset relative to the beginning of the micro tile.)
  quint64 elem_offset = pixel_offset_within_sample + sample_offset_within_micro_tile;

  // How many samples there are in each slice
  quint64 samples_per_slice;
  // ???
  quint64 num_sample_splits;
  // Which slice the sample lies in?
  quint64 sample_slice;
  quint64 tile_slice_bits;

  // If there's more than one sample, and one micro tile can't fit in one split.
  // TODO: some of this might be able to be moved to readimagefromdata?
  // TODO: This is currently undocumented because I have no AA or thick textures to work off of.
  if (num_samples > 1 && m_num_micro_tile_bytes > static_cast<quint64>(m_split_size))
  {
    samples_per_slice = m_split_size / m_bytes_per_sample;
    num_sample_splits = num_samples / samples_per_slice;
    // TODO: Could this be written directly to m_num_samples?
    num_samples = static_cast<quint32>(samples_per_slice);

    tile_slice_bits = m_num_micro_tile_bits / num_sample_splits;
    sample_slice = elem_offset / tile_slice_bits;
    elem_offset %= tile_slice_bits;
  }
  else
  {
    samples_per_slice = num_samples;
    num_sample_splits = 1;
    sample_slice = 0;
  }

  // This might have some correlation with pBitPosition?
  elem_offset /= 8;

  quint32 x_3 = Bit(x, 3);
  quint32 x_4 = Bit(x, 4);

  quint32 y_3 = Bit(y, 3);

  quint64 macro_tile_index_x = x / m_macro_tile_pitch;
  quint64 macro_tile_index_y = y / m_macro_tile_height;

  // The Wii U has 4 RAM chips, here we select a seemingly "random" one using an
  // algorithm to generate one from the coordinates.
  quint32 bank_bit_0 = Bit((y / (16 * m_num_pipes)) ^ x_3);
  quint32 bank_bit_1 = Bit((y / (8 * m_num_pipes)) ^ x_4);
  quint32 bank = MakeByte(bank_bit_0, bank_bit_1);

  // Each of the Wii U's RAM chips has 2 channels, here we select a seemingly
  // "random" one using an algorithm to generate one from the coordinates.
  quint32 pipe = Bit((y_3 ^ x_3));

  // Random bank index <<
  // Number of bank bits
  // Random bank index, shifted over in its correct spot.

  // Shifted bank index |
  // Random pipe index =
  // Three bits containing the random bank and pipe.
  quint64 bank_pipe = (bank << m_pipe_bit_count) | pipe;

  // Number of pipes *
  // Bank index specified by texture =
  // Bank index specified by texture, shifted over in its correct spot.

  // Shifted specified bank index +
  // Specified pipe index =
  // Three bits containing the bank and pipe specified by the texture.
  quint64 swizzle = (m_bank_swizzle << m_pipe_bit_count) | m_pipe_swizzle;

  // The current slice the pixel is in, for 3D textures.
  quint64 sliceIn = slice;

  if (m_tile_mode_info.thickness == TileModeInfo::Thickness::Thick)
    sliceIn /= static_cast<quint32>(m_tile_mode_info.thickness);

  // Algorithm to recalculate bank and pipe?
  bank_pipe ^= (sample_slice * ((m_num_banks >> 1) + 1)) << m_pipe_bit_count ^
               (swizzle + sliceIn * m_tile_mode_info.shared_info.rotation);
  bank_pipe %= m_num_pipes * m_num_banks;
  pipe = bank_pipe % m_num_pipes;
  bank = bank_pipe / m_num_pipes;

  m_slice_offset = m_num_slice_bytes * ((sample_slice + num_sample_splits * slice) /
                                        static_cast<quint32>(m_tile_mode_info.thickness));

  // Y index of the macro tile *
  // Number of macro tiles in each row =
  // Index of the first macro tile in the current row.

  // Index of the first macro tile in the current row +
  // X index of the macro tile =
  // Index of the macro tile.

  // Number of bytes in one macro tile *
  // Index of the macro tile =
  // Offset of the macro tile.
  quint64 macro_tile_offset =
      m_num_macro_tile_bytes * (macro_tile_index_x + m_macro_tiles_per_row * macro_tile_index_y);

  // Do bank swapping if needed
  if (m_tile_mode_info.swap_banks)
  {
    static const quint32 bankSwapOrder[] = {0, 1, 3, 2, 6, 7, 5, 4, 0, 0};
    quint64 bank_swap_width = ComputeSurfaceBankSwappedWidth(m_base_header->pitch);
    quint64 swap_index = m_macro_tile_pitch * macro_tile_index_x / bank_swap_width;
    quint64 bank_mask = m_num_banks - 1;
    bank ^= bankSwapOrder[swap_index & bank_mask];
  }
  // Calculate final offset
  // Get mask targeting every group bit.
  quint64 group_mask = BitMask(m_group_bit_count);
  // Offset of the macro tile +
  // Offset of the slice relative to the beginning of the macro tile =
  // Absolute offset of the slice.

  // Number of bits containing the bank +
  // Number of bits containing the pipe =
  // Number of bits containing the bank and pipe.

  // Absolute offset of the slice >>
  // Number of bits containing the bank and pipe =
  // Shifted absolute offset of the slice.

  // Pixel offset relative to the beginning of the micro tile +
  // Shifted absolute offset of the slice +
  // Offset of the pixel.
  quint64 total_offset =
      elem_offset + ((macro_tile_offset + m_slice_offset) >> (m_bank_bit_count + m_pipe_bit_count));

  // Get the part of the pixel offset left of the pipe and bank.
  quint64 offset_high = (total_offset & ~group_mask) << (m_bank_bit_count + m_pipe_bit_count);
  // Get the part of the pixel offset right of the pipe and bank.
  quint64 offset_low = total_offset & group_mask;
  // Get the actual pipe and bank.
  quint64 bank_bits = bank << (m_pipe_bit_count + m_group_bit_count);
  quint64 pipe_bits = pipe << m_group_bit_count;
  // Put it all together.
  quint64 offset = bank_bits | pipe_bits | offset_low | offset_high;

#ifdef DEBUG
#ifdef VERBOSE
  qDebug() << "-----------------";
  qDebug("Bank (Bin):            %s", QString::number(bank, 2).toStdString().c_str());
  qDebug("Pipe (Bin):              %s", QString::number(pipe, 2).toStdString().c_str());
  qDebug("High Offset:     %s", QString::number(offset_high, 2).toStdString().c_str());
  qDebug("Low Offset:               %s", QString::number(offset_low, 2).toStdString().c_str());
#endif
  qDebug("Final Offset:    %s (AKA: 0x%s)", QString::number(offset, 2).toStdString().c_str(),
         QString::number(offset, 16).toStdString().c_str());
#endif

  return offset;
}

quint32 GX2ImageBase::ComputePixelIndexWithinMicroTile(quint32 x, quint32 y, quint32 z)
{
  quint32 pixel_bit_0 = 0;
  quint32 pixel_bit_1 = 0;
  quint32 pixel_bit_2 = 0;
  quint32 pixel_bit_3 = 0;
  quint32 pixel_bit_4 = 0;
  quint32 pixel_bit_5 = 0;
  quint32 pixel_bit_6 = 0;
  quint32 pixel_bit_7 = 0;
  quint32 pixel_bit_8 = 0;

  quint32 x_0 = Bit(x, 0);
  quint32 x_1 = Bit(x, 1);
  quint32 x_2 = Bit(x, 2);
  quint32 y_0 = Bit(y, 0);
  quint32 y_1 = Bit(y, 1);
  quint32 y_2 = Bit(y, 2);
  quint32 z_0 = Bit(z, 0);
  quint32 z_1 = Bit(z, 1);
  quint32 z_2 = Bit(z, 2);

  // TODO: When is this used?
  if (m_micro_tile_type == MicroTileType::ThickTiliing)
  {
    pixel_bit_0 = x_0;
    pixel_bit_1 = y_0;
    pixel_bit_2 = z_0;
    pixel_bit_3 = x_1;
    pixel_bit_4 = y_1;
    pixel_bit_5 = z_1;
    pixel_bit_6 = x_2;
    pixel_bit_7 = y_2;
  }
  else
  {
    if (m_micro_tile_type != MicroTileType::Displayable)
    {
      pixel_bit_0 = x_0;
      pixel_bit_1 = y_0;
      pixel_bit_2 = x_1;
      pixel_bit_3 = y_1;
      pixel_bit_4 = x_2;
      pixel_bit_5 = y_2;
    }
    else
    {
      switch (m_format_info.shared_info.bpp)
      {
      case 8:
        pixel_bit_0 = x_0;
        pixel_bit_1 = x_1;
        pixel_bit_2 = x_2;
        pixel_bit_3 = y_1;
        pixel_bit_4 = y_0;
        pixel_bit_5 = y_2;
        break;
      case 16:
        pixel_bit_0 = x_0;
        pixel_bit_1 = x_1;
        pixel_bit_2 = x_2;
        pixel_bit_3 = y_0;
        pixel_bit_4 = y_1;
        pixel_bit_5 = y_2;
        break;
      case 64:
        pixel_bit_0 = x_0;
        pixel_bit_1 = y_0;
        pixel_bit_2 = x_1;
        pixel_bit_3 = x_2;
        pixel_bit_4 = y_1;
        pixel_bit_5 = y_2;
        break;
      case 128:
        pixel_bit_0 = y_0;
        pixel_bit_1 = x_0;
        pixel_bit_2 = x_1;
        pixel_bit_3 = x_2;
        pixel_bit_4 = y_1;
        pixel_bit_5 = y_2;
        break;
      case 32:
      case 96:
      default:
        pixel_bit_0 = x_0;
        pixel_bit_1 = x_1;
        pixel_bit_2 = y_0;
        pixel_bit_3 = x_2;
        pixel_bit_4 = y_1;
        pixel_bit_5 = y_2;
        break;
      }
    }

    if (m_tile_mode_info.thickness == TileModeInfo::Thickness::Thick)
    {
      pixel_bit_6 = z_0;
      pixel_bit_7 = z_1;
    }
  }

  if (static_cast<quint32>(m_tile_mode_info.thickness) == 8)
  {
    pixel_bit_8 = z_2;
  }

  return MakeByte(pixel_bit_0, pixel_bit_1, pixel_bit_2, pixel_bit_3, pixel_bit_4, pixel_bit_5,
                  pixel_bit_6, pixel_bit_7, pixel_bit_8);
}

quint32 GX2ImageBase::ComputeSurfaceBankSwappedWidth(quint32 pitch)
{
  quint32 bank_swap_width = 0;
  quint32 slices_per_tile = 1;

  quint32 num_samples = m_num_samples;
  // TODO: Same as m_bytes_per_sample?
  quint32 bytes_per_sample = 8 * m_format_info.shared_info.bpp;
  quint32 samples_per_tile = m_split_size / bytes_per_sample;

  if (m_split_size / m_bytes_per_sample)
  {
    slices_per_tile = qMax<uint32_t>(1u, num_samples / samples_per_tile);
  }

  if (m_tile_mode_info.thickness == TileModeInfo::Thickness::Thick)
    num_samples = 4;

  // Number of samples *
  //
  quint32 bytes_per_tile_slice = num_samples * m_bytes_per_sample / slices_per_tile;

  if (m_tile_mode_info.thickness == TileModeInfo::Thickness::Thick)
  {
    auto swapTiles = qMax<uint32_t>(1u, (m_swap_size >> 1) / m_format_info.shared_info.bpp);
    quint64 swapWidth = swapTiles * 8 * m_num_banks;
    auto heightBytes = num_samples * m_tile_mode_info.aspect_ratio * m_num_pipes *
                       m_format_info.shared_info.bpp / slices_per_tile;
    quint64 swapMax = m_num_pipes * m_num_banks * m_row_size / heightBytes;
    quint64 swapMin = m_pipe_interleave_bytes * 8 * m_num_banks / bytes_per_tile_slice;

    bank_swap_width = qMin(swapMax, qMax(swapMin, swapWidth));

    while (bank_swap_width >= 2 * pitch)
    {
      bank_swap_width >>= 1;
    }
  }

  return bank_swap_width;
}
