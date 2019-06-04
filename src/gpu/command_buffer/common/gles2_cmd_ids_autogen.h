// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is auto-generated from
// gpu/command_buffer/build_gles2_cmd_buffer.py
// It's formatted by clang-format using chromium coding style:
//    clang-format -i -style=chromium filename
// DO NOT EDIT!

#ifndef GPU_COMMAND_BUFFER_COMMON_GLES2_CMD_IDS_AUTOGEN_H_
#define GPU_COMMAND_BUFFER_COMMON_GLES2_CMD_IDS_AUTOGEN_H_

#define GLES2_COMMAND_LIST(OP)                                       \
  OP(ActiveTexture)                                        /* 256 */ \
  OP(AttachShader)                                         /* 257 */ \
  OP(BindAttribLocationBucket)                             /* 258 */ \
  OP(BindBuffer)                                           /* 259 */ \
  OP(BindBufferBase)                                       /* 260 */ \
  OP(BindBufferRange)                                      /* 261 */ \
  OP(BindFramebuffer)                                      /* 262 */ \
  OP(BindRenderbuffer)                                     /* 263 */ \
  OP(BindSampler)                                          /* 264 */ \
  OP(BindTexture)                                          /* 265 */ \
  OP(BindTransformFeedback)                                /* 266 */ \
  OP(BlendColor)                                           /* 267 */ \
  OP(BlendEquation)                                        /* 268 */ \
  OP(BlendEquationSeparate)                                /* 269 */ \
  OP(BlendFunc)                                            /* 270 */ \
  OP(BlendFuncSeparate)                                    /* 271 */ \
  OP(BufferData)                                           /* 272 */ \
  OP(BufferSubData)                                        /* 273 */ \
  OP(CheckFramebufferStatus)                               /* 274 */ \
  OP(Clear)                                                /* 275 */ \
  OP(ClearBufferfi)                                        /* 276 */ \
  OP(ClearBufferfvImmediate)                               /* 277 */ \
  OP(ClearBufferivImmediate)                               /* 278 */ \
  OP(ClearBufferuivImmediate)                              /* 279 */ \
  OP(ClearColor)                                           /* 280 */ \
  OP(ClearDepthf)                                          /* 281 */ \
  OP(ClearStencil)                                         /* 282 */ \
  OP(ClientWaitSync)                                       /* 283 */ \
  OP(ColorMask)                                            /* 284 */ \
  OP(CompileShader)                                        /* 285 */ \
  OP(CompressedTexImage2DBucket)                           /* 286 */ \
  OP(CompressedTexImage2D)                                 /* 287 */ \
  OP(CompressedTexSubImage2DBucket)                        /* 288 */ \
  OP(CompressedTexSubImage2D)                              /* 289 */ \
  OP(CompressedTexImage3DBucket)                           /* 290 */ \
  OP(CompressedTexImage3D)                                 /* 291 */ \
  OP(CompressedTexSubImage3DBucket)                        /* 292 */ \
  OP(CompressedTexSubImage3D)                              /* 293 */ \
  OP(CopyBufferSubData)                                    /* 294 */ \
  OP(CopyTexImage2D)                                       /* 295 */ \
  OP(CopyTexSubImage2D)                                    /* 296 */ \
  OP(CopyTexSubImage3D)                                    /* 297 */ \
  OP(CreateProgram)                                        /* 298 */ \
  OP(CreateShader)                                         /* 299 */ \
  OP(CullFace)                                             /* 300 */ \
  OP(DeleteBuffersImmediate)                               /* 301 */ \
  OP(DeleteFramebuffersImmediate)                          /* 302 */ \
  OP(DeleteProgram)                                        /* 303 */ \
  OP(DeleteRenderbuffersImmediate)                         /* 304 */ \
  OP(DeleteSamplersImmediate)                              /* 305 */ \
  OP(DeleteSync)                                           /* 306 */ \
  OP(DeleteShader)                                         /* 307 */ \
  OP(DeleteTexturesImmediate)                              /* 308 */ \
  OP(DeleteTransformFeedbacksImmediate)                    /* 309 */ \
  OP(DepthFunc)                                            /* 310 */ \
  OP(DepthMask)                                            /* 311 */ \
  OP(DepthRangef)                                          /* 312 */ \
  OP(DetachShader)                                         /* 313 */ \
  OP(Disable)                                              /* 314 */ \
  OP(DisableVertexAttribArray)                             /* 315 */ \
  OP(DrawArrays)                                           /* 316 */ \
  OP(DrawElements)                                         /* 317 */ \
  OP(Enable)                                               /* 318 */ \
  OP(EnableVertexAttribArray)                              /* 319 */ \
  OP(FenceSync)                                            /* 320 */ \
  OP(Finish)                                               /* 321 */ \
  OP(Flush)                                                /* 322 */ \
  OP(FramebufferRenderbuffer)                              /* 323 */ \
  OP(FramebufferTexture2D)                                 /* 324 */ \
  OP(FramebufferTextureLayer)                              /* 325 */ \
  OP(FrontFace)                                            /* 326 */ \
  OP(GenBuffersImmediate)                                  /* 327 */ \
  OP(GenerateMipmap)                                       /* 328 */ \
  OP(GenFramebuffersImmediate)                             /* 329 */ \
  OP(GenRenderbuffersImmediate)                            /* 330 */ \
  OP(GenSamplersImmediate)                                 /* 331 */ \
  OP(GenTexturesImmediate)                                 /* 332 */ \
  OP(GenTransformFeedbacksImmediate)                       /* 333 */ \
  OP(GetActiveAttrib)                                      /* 334 */ \
  OP(GetActiveUniform)                                     /* 335 */ \
  OP(GetActiveUniformBlockiv)                              /* 336 */ \
  OP(GetActiveUniformBlockName)                            /* 337 */ \
  OP(GetActiveUniformsiv)                                  /* 338 */ \
  OP(GetAttachedShaders)                                   /* 339 */ \
  OP(GetAttribLocation)                                    /* 340 */ \
  OP(GetBooleanv)                                          /* 341 */ \
  OP(GetBufferParameteri64v)                               /* 342 */ \
  OP(GetBufferParameteriv)                                 /* 343 */ \
  OP(GetError)                                             /* 344 */ \
  OP(GetFloatv)                                            /* 345 */ \
  OP(GetFragDataLocation)                                  /* 346 */ \
  OP(GetFramebufferAttachmentParameteriv)                  /* 347 */ \
  OP(GetInteger64v)                                        /* 348 */ \
  OP(GetIntegeri_v)                                        /* 349 */ \
  OP(GetInteger64i_v)                                      /* 350 */ \
  OP(GetIntegerv)                                          /* 351 */ \
  OP(GetInternalformativ)                                  /* 352 */ \
  OP(GetProgramiv)                                         /* 353 */ \
  OP(GetProgramInfoLog)                                    /* 354 */ \
  OP(GetRenderbufferParameteriv)                           /* 355 */ \
  OP(GetSamplerParameterfv)                                /* 356 */ \
  OP(GetSamplerParameteriv)                                /* 357 */ \
  OP(GetShaderiv)                                          /* 358 */ \
  OP(GetShaderInfoLog)                                     /* 359 */ \
  OP(GetShaderPrecisionFormat)                             /* 360 */ \
  OP(GetShaderSource)                                      /* 361 */ \
  OP(GetString)                                            /* 362 */ \
  OP(GetSynciv)                                            /* 363 */ \
  OP(GetTexParameterfv)                                    /* 364 */ \
  OP(GetTexParameteriv)                                    /* 365 */ \
  OP(GetTransformFeedbackVarying)                          /* 366 */ \
  OP(GetUniformBlockIndex)                                 /* 367 */ \
  OP(GetUniformfv)                                         /* 368 */ \
  OP(GetUniformiv)                                         /* 369 */ \
  OP(GetUniformuiv)                                        /* 370 */ \
  OP(GetUniformIndices)                                    /* 371 */ \
  OP(GetUniformLocation)                                   /* 372 */ \
  OP(GetVertexAttribfv)                                    /* 373 */ \
  OP(GetVertexAttribiv)                                    /* 374 */ \
  OP(GetVertexAttribIiv)                                   /* 375 */ \
  OP(GetVertexAttribIuiv)                                  /* 376 */ \
  OP(GetVertexAttribPointerv)                              /* 377 */ \
  OP(Hint)                                                 /* 378 */ \
  OP(InvalidateFramebufferImmediate)                       /* 379 */ \
  OP(InvalidateSubFramebufferImmediate)                    /* 380 */ \
  OP(IsBuffer)                                             /* 381 */ \
  OP(IsEnabled)                                            /* 382 */ \
  OP(IsFramebuffer)                                        /* 383 */ \
  OP(IsProgram)                                            /* 384 */ \
  OP(IsRenderbuffer)                                       /* 385 */ \
  OP(IsSampler)                                            /* 386 */ \
  OP(IsShader)                                             /* 387 */ \
  OP(IsSync)                                               /* 388 */ \
  OP(IsTexture)                                            /* 389 */ \
  OP(IsTransformFeedback)                                  /* 390 */ \
  OP(LineWidth)                                            /* 391 */ \
  OP(LinkProgram)                                          /* 392 */ \
  OP(PauseTransformFeedback)                               /* 393 */ \
  OP(PixelStorei)                                          /* 394 */ \
  OP(PolygonOffset)                                        /* 395 */ \
  OP(ReadBuffer)                                           /* 396 */ \
  OP(ReadPixels)                                           /* 397 */ \
  OP(ReleaseShaderCompiler)                                /* 398 */ \
  OP(RenderbufferStorage)                                  /* 399 */ \
  OP(ResumeTransformFeedback)                              /* 400 */ \
  OP(SampleCoverage)                                       /* 401 */ \
  OP(SamplerParameterf)                                    /* 402 */ \
  OP(SamplerParameterfvImmediate)                          /* 403 */ \
  OP(SamplerParameteri)                                    /* 404 */ \
  OP(SamplerParameterivImmediate)                          /* 405 */ \
  OP(Scissor)                                              /* 406 */ \
  OP(ShaderBinary)                                         /* 407 */ \
  OP(ShaderSourceBucket)                                   /* 408 */ \
  OP(StencilFunc)                                          /* 409 */ \
  OP(StencilFuncSeparate)                                  /* 410 */ \
  OP(StencilMask)                                          /* 411 */ \
  OP(StencilMaskSeparate)                                  /* 412 */ \
  OP(StencilOp)                                            /* 413 */ \
  OP(StencilOpSeparate)                                    /* 414 */ \
  OP(TexImage2D)                                           /* 415 */ \
  OP(TexImage3D)                                           /* 416 */ \
  OP(TexParameterf)                                        /* 417 */ \
  OP(TexParameterfvImmediate)                              /* 418 */ \
  OP(TexParameteri)                                        /* 419 */ \
  OP(TexParameterivImmediate)                              /* 420 */ \
  OP(TexStorage3D)                                         /* 421 */ \
  OP(TexSubImage2D)                                        /* 422 */ \
  OP(TexSubImage3D)                                        /* 423 */ \
  OP(TransformFeedbackVaryingsBucket)                      /* 424 */ \
  OP(Uniform1f)                                            /* 425 */ \
  OP(Uniform1fvImmediate)                                  /* 426 */ \
  OP(Uniform1i)                                            /* 427 */ \
  OP(Uniform1ivImmediate)                                  /* 428 */ \
  OP(Uniform1ui)                                           /* 429 */ \
  OP(Uniform1uivImmediate)                                 /* 430 */ \
  OP(Uniform2f)                                            /* 431 */ \
  OP(Uniform2fvImmediate)                                  /* 432 */ \
  OP(Uniform2i)                                            /* 433 */ \
  OP(Uniform2ivImmediate)                                  /* 434 */ \
  OP(Uniform2ui)                                           /* 435 */ \
  OP(Uniform2uivImmediate)                                 /* 436 */ \
  OP(Uniform3f)                                            /* 437 */ \
  OP(Uniform3fvImmediate)                                  /* 438 */ \
  OP(Uniform3i)                                            /* 439 */ \
  OP(Uniform3ivImmediate)                                  /* 440 */ \
  OP(Uniform3ui)                                           /* 441 */ \
  OP(Uniform3uivImmediate)                                 /* 442 */ \
  OP(Uniform4f)                                            /* 443 */ \
  OP(Uniform4fvImmediate)                                  /* 444 */ \
  OP(Uniform4i)                                            /* 445 */ \
  OP(Uniform4ivImmediate)                                  /* 446 */ \
  OP(Uniform4ui)                                           /* 447 */ \
  OP(Uniform4uivImmediate)                                 /* 448 */ \
  OP(UniformBlockBinding)                                  /* 449 */ \
  OP(UniformMatrix2fvImmediate)                            /* 450 */ \
  OP(UniformMatrix2x3fvImmediate)                          /* 451 */ \
  OP(UniformMatrix2x4fvImmediate)                          /* 452 */ \
  OP(UniformMatrix3fvImmediate)                            /* 453 */ \
  OP(UniformMatrix3x2fvImmediate)                          /* 454 */ \
  OP(UniformMatrix3x4fvImmediate)                          /* 455 */ \
  OP(UniformMatrix4fvImmediate)                            /* 456 */ \
  OP(UniformMatrix4x2fvImmediate)                          /* 457 */ \
  OP(UniformMatrix4x3fvImmediate)                          /* 458 */ \
  OP(UseProgram)                                           /* 459 */ \
  OP(ValidateProgram)                                      /* 460 */ \
  OP(VertexAttrib1f)                                       /* 461 */ \
  OP(VertexAttrib1fvImmediate)                             /* 462 */ \
  OP(VertexAttrib2f)                                       /* 463 */ \
  OP(VertexAttrib2fvImmediate)                             /* 464 */ \
  OP(VertexAttrib3f)                                       /* 465 */ \
  OP(VertexAttrib3fvImmediate)                             /* 466 */ \
  OP(VertexAttrib4f)                                       /* 467 */ \
  OP(VertexAttrib4fvImmediate)                             /* 468 */ \
  OP(VertexAttribI4i)                                      /* 469 */ \
  OP(VertexAttribI4ivImmediate)                            /* 470 */ \
  OP(VertexAttribI4ui)                                     /* 471 */ \
  OP(VertexAttribI4uivImmediate)                           /* 472 */ \
  OP(VertexAttribIPointer)                                 /* 473 */ \
  OP(VertexAttribPointer)                                  /* 474 */ \
  OP(Viewport)                                             /* 475 */ \
  OP(WaitSync)                                             /* 476 */ \
  OP(BlitFramebufferCHROMIUM)                              /* 477 */ \
  OP(RenderbufferStorageMultisampleCHROMIUM)               /* 478 */ \
  OP(RenderbufferStorageMultisampleEXT)                    /* 479 */ \
  OP(FramebufferTexture2DMultisampleEXT)                   /* 480 */ \
  OP(TexStorage2DEXT)                                      /* 481 */ \
  OP(GenQueriesEXTImmediate)                               /* 482 */ \
  OP(DeleteQueriesEXTImmediate)                            /* 483 */ \
  OP(QueryCounterEXT)                                      /* 484 */ \
  OP(BeginQueryEXT)                                        /* 485 */ \
  OP(BeginTransformFeedback)                               /* 486 */ \
  OP(EndQueryEXT)                                          /* 487 */ \
  OP(EndTransformFeedback)                                 /* 488 */ \
  OP(SetDisjointValueSyncCHROMIUM)                         /* 489 */ \
  OP(InsertEventMarkerEXT)                                 /* 490 */ \
  OP(PushGroupMarkerEXT)                                   /* 491 */ \
  OP(PopGroupMarkerEXT)                                    /* 492 */ \
  OP(GenVertexArraysOESImmediate)                          /* 493 */ \
  OP(DeleteVertexArraysOESImmediate)                       /* 494 */ \
  OP(IsVertexArrayOES)                                     /* 495 */ \
  OP(BindVertexArrayOES)                                   /* 496 */ \
  OP(FramebufferParameteri)                                /* 497 */ \
  OP(BindImageTexture)                                     /* 498 */ \
  OP(DispatchCompute)                                      /* 499 */ \
  OP(MemoryBarrierEXT)                                     /* 500 */ \
  OP(MemoryBarrierByRegion)                                /* 501 */ \
  OP(SwapBuffers)                                          /* 502 */ \
  OP(GetMaxValueInBufferCHROMIUM)                          /* 503 */ \
  OP(EnableFeatureCHROMIUM)                                /* 504 */ \
  OP(MapBufferRange)                                       /* 505 */ \
  OP(UnmapBuffer)                                          /* 506 */ \
  OP(FlushMappedBufferRange)                               /* 507 */ \
  OP(ResizeCHROMIUM)                                       /* 508 */ \
  OP(GetRequestableExtensionsCHROMIUM)                     /* 509 */ \
  OP(RequestExtensionCHROMIUM)                             /* 510 */ \
  OP(GetProgramInfoCHROMIUM)                               /* 511 */ \
  OP(GetUniformBlocksCHROMIUM)                             /* 512 */ \
  OP(GetTransformFeedbackVaryingsCHROMIUM)                 /* 513 */ \
  OP(GetUniformsES3CHROMIUM)                               /* 514 */ \
  OP(DescheduleUntilFinishedCHROMIUM)                      /* 515 */ \
  OP(GetTranslatedShaderSourceANGLE)                       /* 516 */ \
  OP(PostSubBufferCHROMIUM)                                /* 517 */ \
  OP(CopyTextureCHROMIUM)                                  /* 518 */ \
  OP(CopySubTextureCHROMIUM)                               /* 519 */ \
  OP(DrawArraysInstancedANGLE)                             /* 520 */ \
  OP(DrawElementsInstancedANGLE)                           /* 521 */ \
  OP(VertexAttribDivisorANGLE)                             /* 522 */ \
  OP(ProduceTextureDirectCHROMIUMImmediate)                /* 523 */ \
  OP(CreateAndConsumeTextureINTERNALImmediate)             /* 524 */ \
  OP(BindUniformLocationCHROMIUMBucket)                    /* 525 */ \
  OP(BindTexImage2DCHROMIUM)                               /* 526 */ \
  OP(BindTexImage2DWithInternalformatCHROMIUM)             /* 527 */ \
  OP(ReleaseTexImage2DCHROMIUM)                            /* 528 */ \
  OP(TraceBeginCHROMIUM)                                   /* 529 */ \
  OP(TraceEndCHROMIUM)                                     /* 530 */ \
  OP(DiscardFramebufferEXTImmediate)                       /* 531 */ \
  OP(LoseContextCHROMIUM)                                  /* 532 */ \
  OP(InsertFenceSyncCHROMIUM)                              /* 533 */ \
  OP(WaitSyncTokenCHROMIUM)                                /* 534 */ \
  OP(UnpremultiplyAndDitherCopyCHROMIUM)                   /* 535 */ \
  OP(DrawBuffersEXTImmediate)                              /* 536 */ \
  OP(DiscardBackbufferCHROMIUM)                            /* 537 */ \
  OP(ScheduleOverlayPlaneCHROMIUM)                         /* 538 */ \
  OP(ScheduleCALayerSharedStateCHROMIUM)                   /* 539 */ \
  OP(ScheduleCALayerCHROMIUM)                              /* 540 */ \
  OP(ScheduleCALayerInUseQueryCHROMIUMImmediate)           /* 541 */ \
  OP(CommitOverlayPlanesCHROMIUM)                          /* 542 */ \
  OP(FlushDriverCachesCHROMIUM)                            /* 543 */ \
  OP(ScheduleDCLayerSharedStateCHROMIUM)                   /* 544 */ \
  OP(ScheduleDCLayerCHROMIUM)                              /* 545 */ \
  OP(SetActiveURLCHROMIUM)                                 /* 546 */ \
  OP(MatrixLoadfCHROMIUMImmediate)                         /* 547 */ \
  OP(MatrixLoadIdentityCHROMIUM)                           /* 548 */ \
  OP(GenPathsCHROMIUM)                                     /* 549 */ \
  OP(DeletePathsCHROMIUM)                                  /* 550 */ \
  OP(IsPathCHROMIUM)                                       /* 551 */ \
  OP(PathCommandsCHROMIUM)                                 /* 552 */ \
  OP(PathParameterfCHROMIUM)                               /* 553 */ \
  OP(PathParameteriCHROMIUM)                               /* 554 */ \
  OP(PathStencilFuncCHROMIUM)                              /* 555 */ \
  OP(StencilFillPathCHROMIUM)                              /* 556 */ \
  OP(StencilStrokePathCHROMIUM)                            /* 557 */ \
  OP(CoverFillPathCHROMIUM)                                /* 558 */ \
  OP(CoverStrokePathCHROMIUM)                              /* 559 */ \
  OP(StencilThenCoverFillPathCHROMIUM)                     /* 560 */ \
  OP(StencilThenCoverStrokePathCHROMIUM)                   /* 561 */ \
  OP(StencilFillPathInstancedCHROMIUM)                     /* 562 */ \
  OP(StencilStrokePathInstancedCHROMIUM)                   /* 563 */ \
  OP(CoverFillPathInstancedCHROMIUM)                       /* 564 */ \
  OP(CoverStrokePathInstancedCHROMIUM)                     /* 565 */ \
  OP(StencilThenCoverFillPathInstancedCHROMIUM)            /* 566 */ \
  OP(StencilThenCoverStrokePathInstancedCHROMIUM)          /* 567 */ \
  OP(BindFragmentInputLocationCHROMIUMBucket)              /* 568 */ \
  OP(ProgramPathFragmentInputGenCHROMIUM)                  /* 569 */ \
  OP(CoverageModulationCHROMIUM)                           /* 570 */ \
  OP(BlendBarrierKHR)                                      /* 571 */ \
  OP(ApplyScreenSpaceAntialiasingCHROMIUM)                 /* 572 */ \
  OP(BindFragDataLocationIndexedEXTBucket)                 /* 573 */ \
  OP(BindFragDataLocationEXTBucket)                        /* 574 */ \
  OP(GetFragDataIndexEXT)                                  /* 575 */ \
  OP(UniformMatrix4fvStreamTextureMatrixCHROMIUMImmediate) /* 576 */ \
  OP(OverlayPromotionHintCHROMIUM)                         /* 577 */ \
  OP(SwapBuffersWithBoundsCHROMIUMImmediate)               /* 578 */ \
  OP(SetDrawRectangleCHROMIUM)                             /* 579 */ \
  OP(SetEnableDCLayersCHROMIUM)                            /* 580 */ \
  OP(InitializeDiscardableTextureCHROMIUM)                 /* 581 */ \
  OP(UnlockDiscardableTextureCHROMIUM)                     /* 582 */ \
  OP(LockDiscardableTextureCHROMIUM)                       /* 583 */ \
  OP(TexStorage2DImageCHROMIUM)                            /* 584 */ \
  OP(SetColorSpaceMetadataCHROMIUM)                        /* 585 */ \
  OP(WindowRectanglesEXTImmediate)                         /* 586 */ \
  OP(CreateGpuFenceINTERNAL)                               /* 587 */ \
  OP(WaitGpuFenceCHROMIUM)                                 /* 588 */ \
  OP(DestroyGpuFenceCHROMIUM)                              /* 589 */ \
  OP(SetReadbackBufferShadowAllocationINTERNAL)            /* 590 */ \
  OP(FramebufferTextureMultiviewLayeredANGLE)              /* 591 */ \
  OP(MaxShaderCompilerThreadsKHR)                          /* 592 */ \
  OP(CreateAndTexStorage2DSharedImageINTERNALImmediate)    /* 593 */ \
  OP(BeginSharedImageAccessDirectCHROMIUM)                 /* 594 */ \
  OP(EndSharedImageAccessDirectCHROMIUM)                   /* 595 */

enum CommandId {
  kOneBeforeStartPoint =
      cmd::kLastCommonId,  // All GLES2 commands start after this.
#define GLES2_CMD_OP(name) k##name,
  GLES2_COMMAND_LIST(GLES2_CMD_OP)
#undef GLES2_CMD_OP
      kNumCommands,
  kFirstGLES2Command = kOneBeforeStartPoint + 1
};

#endif  // GPU_COMMAND_BUFFER_COMMON_GLES2_CMD_IDS_AUTOGEN_H_
