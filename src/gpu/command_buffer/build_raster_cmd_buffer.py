#!/usr/bin/env python
# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""code generator for raster command buffers."""

import os
import os.path
import sys
from optparse import OptionParser

import build_cmd_buffer_lib

# Named type info object represents a named type that is used in OpenGL call
# arguments.  Each named type defines a set of valid OpenGL call arguments.  The
# named types are used in 'raster_cmd_buffer_functions.txt'.
# type: The actual GL type of the named type.
# valid: The list of values that are valid for both the client and the service.
# invalid: Examples of invalid values for the type. At least these values
#          should be tested to be invalid.
# is_complete: The list of valid values of type are final and will not be
#              modified during runtime.
# validator: If set to False will prevent creation of a ValueValidator. Values
#            are still expected to be checked for validity and will be tested.
_NAMED_TYPE_INFO = {
  'GLState': {
    'type': 'GLenum',
    'valid': [
      'GL_ACTIVE_TEXTURE',
    ],
    'invalid': [
      'GL_FOG_HINT',
    ],
  },
  'QueryObjectParameter': {
    'type': 'GLenum',
    'is_complete': True,
    'valid': [
      'GL_QUERY_RESULT_EXT',
      'GL_QUERY_RESULT_AVAILABLE_EXT',
      'GL_QUERY_RESULT_AVAILABLE_NO_FLUSH_CHROMIUM_EXT',
    ],
  },
  'QueryTarget': {
    'type': 'GLenum',
    'is_complete': True,
    'valid': [
      'GL_COMMANDS_ISSUED_CHROMIUM',
      'GL_COMMANDS_COMPLETED_CHROMIUM',
    ],
    'invalid': [
      'GL_LATENCY_QUERY_CHROMIUM',
    ],
  },
  'TextureParameter': {
    'type': 'GLenum',
    'valid': [
      'GL_TEXTURE_MAG_FILTER',
      'GL_TEXTURE_MIN_FILTER',
      'GL_TEXTURE_WRAP_S',
      'GL_TEXTURE_WRAP_T',
    ],
    'invalid': [
      'GL_GENERATE_MIPMAP',
    ],
  },
  'TextureWrapMode': {
    'type': 'GLenum',
    'valid': [
      'GL_CLAMP_TO_EDGE',
    ],
    'invalid': [
      'GL_REPEAT',
    ],
  },
  'TextureMinFilterMode': {
    'type': 'GLenum',
    'valid': [
      'GL_NEAREST',
    ],
    'invalid': [
      'GL_NEAREST_MIPMAP_NEAREST',
    ],
  },
  'TextureMagFilterMode': {
    'type': 'GLenum',
    'valid': [
      'GL_NEAREST',
    ],
    'invalid': [
      'GL_LINEAR',
    ],
  },
  'ResetStatus': {
    'type': 'GLenum',
    'is_complete': True,
    'valid': [
      'GL_GUILTY_CONTEXT_RESET_ARB',
      'GL_INNOCENT_CONTEXT_RESET_ARB',
      'GL_UNKNOWN_CONTEXT_RESET_ARB',
    ],
  },
  'gfx::BufferUsage': {
    'type': 'gfx::BufferUsage',
    'valid': [
      'gfx::BufferUsage::GPU_READ',
      'gfx::BufferUsage::SCANOUT',
      'gfx::BufferUsage::GPU_READ_CPU_READ_WRITE',
      'gfx::BufferUsage::GPU_READ_CPU_READ_WRITE_PERSISTENT',
    ],
    'invalid': [
      'gfx::BufferUsage::SCANOUT_CAMERA_READ_WRITE',
      'gfx::BufferUsage::CAMERA_AND_CPU_READ_WRITE',
    ],
  },
  'viz::ResourceFormat': {
    'type': 'viz::ResourceFormat',
    'valid': [
      'viz::ResourceFormat::RGBA_8888',
      'viz::ResourceFormat::RGBA_4444',
      'viz::ResourceFormat::BGRA_8888',
      'viz::ResourceFormat::ALPHA_8',
      'viz::ResourceFormat::LUMINANCE_8',
      'viz::ResourceFormat::RGB_565',
      'viz::ResourceFormat::BGR_565',
      'viz::ResourceFormat::RED_8',
      'viz::ResourceFormat::RG_88',
      'viz::ResourceFormat::LUMINANCE_F16',
      'viz::ResourceFormat::RGBA_F16',
      'viz::ResourceFormat::R16_EXT',
      'viz::ResourceFormat::RGBX_8888',
      'viz::ResourceFormat::BGRX_8888',
      'viz::ResourceFormat::RGBX_1010102',
      'viz::ResourceFormat::BGRX_1010102',
      'viz::ResourceFormat::YVU_420',
      'viz::ResourceFormat::YUV_420_BIPLANAR',
      'viz::ResourceFormat::UYVY_422',

    ],
    'invalid': [
      'viz::ResourceFormat::ETC1',
    ],
  },
}

# A function info object specifies the type and other special data for the
# command that will be generated. A base function info object is generated by
# parsing the "raster_cmd_buffer_functions.txt", one for each function in the
# file. These function info objects can be augmented and their values can be
# overridden by adding an object to the table below.
#
# Must match function names specified in "raster_cmd_buffer_functions.txt".
#
# type:         defines which handler will be used to generate code.
# decoder_func: defines which function to call in the decoder to execute the
#               corresponding GL command. If not specified the GL command will
#               be called directly.
# cmd_args:     The arguments to use for the command. This overrides generating
#               them based on the GL function arguments.
# data_transfer_methods: Array of methods that are used for transfering the
#               pointer data.  Possible values: 'immediate', 'shm', 'bucket'.
#               The default is 'immediate' if the command has one pointer
#               argument, otherwise 'shm'. One command is generated for each
#               transfer method. Affects only commands which are not of type
#               'GETn' or 'GLcharN'.
#               Note: the command arguments that affect this are the final args,
#               taking cmd_args override into consideration.
# impl_func:    Whether or not to generate the GLES2Implementation part of this
#               command.
# internal:     If true, this is an internal command only, not exposed to the
#               client.
# count:        The number of units per element. For PUTn or PUT types.
# use_count_func: If True the actual data count needs to be computed; the count
#               argument specifies the maximum count.
# unit_test:    If False no service side unit test will be generated.
# client_test:  If False no client side unit test will be generated.
# expectation:  If False the unit test will have no expected calls.
# valid_args:   A dictionary of argument indices to args to use in unit tests
#               when they can not be automatically determined.
# invalid_test: False if no invalid test needed.
# not_shared:   For GENn types, True if objects can't be shared between contexts

_FUNCTION_INFO = {
  'CreateAndConsumeTexture': {
    'type': 'NoCommand',
    'trace_level': 2,
  },
  'CreateAndConsumeTextureINTERNAL': {
    'decoder_func': 'DoCreateAndConsumeTextureINTERNAL',
    'internal': True,
    'type': 'PUT',
    'count': 16,  # GL_MAILBOX_SIZE_CHROMIUM
    'unit_test': False,
    'trace_level': 2,
  },
  'CreateImageCHROMIUM': {
    'type': 'NoCommand',
    'cmd_args':
        'ClientBuffer buffer, GLsizei width, GLsizei height, '
        'GLenum internalformat',
    'result': ['GLuint'],
    'trace_level': 1,
  },
  'CopySubTexture': {
    'decoder_func': 'DoCopySubTexture',
    'unit_test': False,
    'trace_level': 2,
  },
  'DestroyImageCHROMIUM': {
    'type': 'NoCommand',
    'trace_level': 1,
  },
  'DeleteTextures': {
    'type': 'DELn',
    'resource_type': 'Texture',
    'resource_types': 'Textures',
  },
  'Finish': {
    'impl_func': False,
    'client_test': False,
    'decoder_func': 'DoFinish',
    'trace_level': 1,
  },
  'Flush': {
    'impl_func': False,
    'decoder_func': 'DoFlush',
    'trace_level': 1,
  },
  'GetError': {
    'type': 'Is',
    'decoder_func': 'GetErrorState()->GetGLError',
    'impl_func': False,
    'result': ['GLenum'],
    'client_test': False,
  },
  'GetGraphicsResetStatusKHR': {
    'type': 'NoCommand',
    'trace_level': 1,
  },
  'GetIntegerv': {
    'type': 'GETn',
    'result': ['SizedResult<GLint>'],
    'decoder_func': 'DoGetIntegerv',
    'client_test': False,
  },
  'ProduceTextureDirect': {
    'decoder_func': 'DoProduceTextureDirect',
    'impl_func': False,
    'type': 'PUT',
    'count': 16,  # GL_MAILBOX_SIZE_CHROMIUM
    'unit_test': False,
    'client_test': False,
    'trace_level': 1,
  },
  'TexParameteri': {
    'decoder_func': 'DoTexParameteri',
    'unit_test' : False,
    'valid_args': {
      '2': 'GL_NEAREST'
    },
  },
  'TexStorage2D': {
    'decoder_func': 'DoTexStorage2D',
    'unit_test': False,
  },
  'WaitSync': {
    'type': 'Custom',
    'cmd_args': 'GLuint sync, GLbitfieldSyncFlushFlags flags, '
                'GLuint64 timeout',
    'impl_func': False,
    'client_test': False,
    'trace_level': 1,
  },
  'GenQueriesEXT': {
    'type': 'GENn',
    'gl_test_func': 'glGenQueriesARB',
    'resource_type': 'Query',
    'resource_types': 'Queries',
    'unit_test': False,
    'not_shared': 'True',
  },
  'DeleteQueriesEXT': {
    'type': 'DELn',
    'gl_test_func': 'glDeleteQueriesARB',
    'resource_type': 'Query',
    'resource_types': 'Queries',
    'unit_test': False,
  },
  'BeginQueryEXT': {
    'type': 'Custom',
    'impl_func': False,
    'cmd_args': 'GLenumQueryTarget target, GLidQuery id, void* sync_data',
    'data_transfer_methods': ['shm'],
    'gl_test_func': 'glBeginQuery',
  },
  'EndQueryEXT': {
    'type': 'Custom',
    'impl_func': False,
    'cmd_args': 'GLenumQueryTarget target, GLuint submit_count',
    'gl_test_func': 'glEndnQuery',
    'client_test': False,
  },
  'GetQueryObjectuivEXT': {
    'type': 'NoCommand',
    'gl_test_func': 'glGetQueryObjectuiv',
  },
  'BindTexImage2DCHROMIUM': {
    'decoder_func': 'DoBindTexImage2DCHROMIUM',
    'unit_test': False,
  },
  'ReleaseTexImage2DCHROMIUM': {
    'decoder_func': 'DoReleaseTexImage2DCHROMIUM',
    'unit_test': False,
  },
  'ShallowFlushCHROMIUM': {
    'type': 'NoCommand',
  },
  'OrderingBarrierCHROMIUM': {
    'type': 'NoCommand',
  },
  'TraceBeginCHROMIUM': {
    'type': 'Custom',
    'impl_func': False,
    'client_test': False,
    'cmd_args': 'GLuint category_bucket_id, GLuint name_bucket_id',
    'extension': 'CHROMIUM_trace_marker',
  },
  'TraceEndCHROMIUM': {
    'impl_func': False,
    'client_test': False,
    'decoder_func': 'DoTraceEndCHROMIUM',
    'unit_test': False,
    'extension': 'CHROMIUM_trace_marker',
  },
  'SetActiveURLCHROMIUM': {
    'type': 'Custom',
    'impl_func': False,
    'client_test': False,
    'cmd_args': 'GLuint url_bucket_id',
  },
  'InsertFenceSyncCHROMIUM': {
    'type': 'Custom',
    'internal': True,
    'impl_func': False,
    'cmd_args': 'GLuint64 release_count',
    'trace_level': 1,
  },
  'LoseContextCHROMIUM': {
    'decoder_func': 'DoLoseContextCHROMIUM',
    'unit_test': False,
    'trace_level': 1,
  },
  'GenSyncTokenCHROMIUM': {
    'type': 'NoCommand',
  },
  'GenUnverifiedSyncTokenCHROMIUM': {
    'type': 'NoCommand',
  },
  'VerifySyncTokensCHROMIUM' : {
    'type': 'NoCommand',
  },
  'WaitSyncTokenCHROMIUM': {
    'type': 'Custom',
    'impl_func': False,
    'cmd_args': 'GLint namespace_id, '
                'GLuint64 command_buffer_id, '
                'GLuint64 release_count',
    'client_test': False,
  },
  'InitializeDiscardableTextureCHROMIUM': {
    'type': 'Custom',
    'cmd_args': 'GLuint texture_id, uint32_t shm_id, '
                'uint32_t shm_offset',
    'impl_func': False,
    'client_test': False,
  },
  'UnlockDiscardableTextureCHROMIUM': {
    'type': 'Custom',
    'cmd_args': 'GLuint texture_id',
    'impl_func': False,
    'client_test': False,
  },
  'LockDiscardableTextureCHROMIUM': {
    'type': 'Custom',
    'cmd_args': 'GLuint texture_id',
    'impl_func': False,
    'client_test': False,
  },
  'BeginRasterCHROMIUM': {
    'decoder_func': 'DoBeginRasterCHROMIUM',
    'type': 'PUT',
    'count': 16,  # GL_MAILBOX_SIZE_CHROMIUM
    'internal': True,
    'impl_func': False,
    'unit_test': False,
  },
  'RasterCHROMIUM': {
    'decoder_func': 'DoRasterCHROMIUM',
    'internal': True,
    'impl_func': True,
    'cmd_args': 'GLuint raster_shm_id, GLuint raster_shm_offset,'
                'GLsizeiptr raster_shm_size, GLuint font_shm_id,'
                'GLuint font_shm_offset, GLsizeiptr font_shm_size',
    'extension': 'CHROMIUM_raster_transport',
    'extension_flag': 'chromium_raster_transport',
  },
  'EndRasterCHROMIUM': {
    'decoder_func': 'DoEndRasterCHROMIUM',
    'impl_func': False,
    'unit_test': False,
    'client_test': False,
  },
  'CreateTransferCacheEntryINTERNAL': {
    'decoder_func': 'DoCreateTransferCacheEntryINTERNAL',
    'cmd_args': 'GLuint entry_type, GLuint entry_id, GLuint handle_shm_id, '
                'GLuint handle_shm_offset, GLuint data_shm_id, '
                'GLuint data_shm_offset, GLuint data_size',
    'internal': True,
    'impl_func': True,
    'client_test': False,
    'unit_test': False,
  },
  'DeleteTransferCacheEntryINTERNAL': {
    'decoder_func': 'DoDeleteTransferCacheEntryINTERNAL',
    'cmd_args': 'GLuint entry_type, GLuint entry_id',
    'internal': True,
    'impl_func': True,
    'client_test': False,
    'unit_test': False,
  },
  'DeletePaintCacheTextBlobsINTERNAL': {
    'type': 'DELn',
    'internal': True,
    'unit_test': False,
  },
  'DeletePaintCachePathsINTERNAL': {
    'type': 'DELn',
    'internal': True,
    'unit_test': False,
  },
  'ClearPaintCacheINTERNAL': {
    'decoder_func': 'DoClearPaintCacheINTERNAL',
    'internal': True,
    'unit_test': False,
  },
  'UnlockTransferCacheEntryINTERNAL': {
    'decoder_func': 'DoUnlockTransferCacheEntryINTERNAL',
    'cmd_args': 'GLuint entry_type, GLuint entry_id',
    'internal': True,
    'impl_func': True,
    'client_test': False,
    'unit_test': False,
  },
  'CreateTexture': {
    'type': 'Create',
    'resource_type': 'Texture',
    'resource_types': 'Textures',
    'decoder_func': 'DoCreateTexture',
    'not_shared': 'True',
    'unit_test': False,
  },
  'SetColorSpaceMetadata': {
    'type': 'Custom',
    'impl_func': False,
    'client_test': False,
    'cmd_args': 'GLuint texture_id, GLuint shm_id, GLuint shm_offset, '
                'GLsizei color_space_size',
  },
  'UnpremultiplyAndDitherCopyCHROMIUM': {
    'decoder_func': 'DoUnpremultiplyAndDitherCopyCHROMIUM',
    'cmd_args': 'GLuint source_id, GLuint dest_id, GLint x, GLint y, '
                'GLsizei width, GLsizei height',
    'client_test': False,
    'unit_test': False,
    'impl_func': True,
  },
}


def main(argv):
  """This is the main function."""
  parser = OptionParser()
  parser.add_option(
      "--output-dir",
      help="base directory for resulting files, under chrome/src. default is "
      "empty. Use this if you want the result stored under gen.")
  parser.add_option(
      "-v", "--verbose", action="store_true",
      help="prints more output.")

  (options, _) = parser.parse_args(args=argv)

  # This script lives under gpu/command_buffer, cd to base directory.
  os.chdir(os.path.dirname(__file__) + "/../..")
  base_dir = os.getcwd()
  build_cmd_buffer_lib.InitializePrefix("Raster")
  gen = build_cmd_buffer_lib.GLGenerator(options.verbose, "2018",
                                         _FUNCTION_INFO, _NAMED_TYPE_INFO)
  gen.ParseGLH("gpu/command_buffer/raster_cmd_buffer_functions.txt")

  # Support generating files under gen/
  if options.output_dir != None:
    os.chdir(options.output_dir)

  os.chdir(base_dir)

  gen.WriteCommandIds("gpu/command_buffer/common/raster_cmd_ids_autogen.h")
  gen.WriteFormat("gpu/command_buffer/common/raster_cmd_format_autogen.h")
  gen.WriteFormatTest(
    "gpu/command_buffer/common/raster_cmd_format_test_autogen.h")
  gen.WriteGLES2InterfaceHeader(
    "gpu/command_buffer/client/raster_interface_autogen.h")
  gen.WriteGLES2ImplementationHeader(
    "gpu/command_buffer/client/raster_implementation_autogen.h")
  gen.WriteGLES2Implementation(
    "gpu/command_buffer/client/raster_implementation_impl_autogen.h")
  gen.WriteGLES2ImplementationUnitTests(
    "gpu/command_buffer/client/raster_implementation_unittest_autogen.h")
  gen.WriteCmdHelperHeader(
     "gpu/command_buffer/client/raster_cmd_helper_autogen.h")
  gen.WriteServiceImplementation(
    "gpu/command_buffer/service/raster_decoder_autogen.h")
  gen.WriteServiceUnitTests(
    "gpu/command_buffer/service/raster_decoder_unittest_%d_autogen.h")
  gen.WriteServiceUtilsHeader(
    "gpu/command_buffer/service/raster_cmd_validation_autogen.h")
  gen.WriteServiceUtilsImplementation(
    "gpu/command_buffer/service/"
    "raster_cmd_validation_implementation_autogen.h")

  build_cmd_buffer_lib.Format(gen.generated_cpp_filenames)

  if gen.errors > 0:
    print "%d errors" % gen.errors
    return 1
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
