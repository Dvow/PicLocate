option(PICLOCATE_GPU "Bundle optional DirectML acceleration on Windows" ON)
if(PICLOCATE_GPU AND WIN32)
  CPMAddPackage(NAME PicLocateOrtDml VERSION 1.24.4 DOWNLOAD_ONLY YES
    URL "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg"
    URL_HASH SHA256=57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681)
  CPMAddPackage(NAME PicLocateDirectML VERSION 1.15.4 DOWNLOAD_ONLY YES
    URL "https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.4/microsoft.ai.directml.1.15.4.nupkg"
    URL_HASH SHA256=4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9)
  set(_gpu_runtime "${PicLocateOrtDml_SOURCE_DIR}/runtimes/win-${PICLOCATE_ARCH}/native/onnxruntime.dll")
  set(_gpu_directml "${PicLocateDirectML_SOURCE_DIR}/bin/${PICLOCATE_ARCH}-win/DirectML.dll")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/gpu")
  configure_file("${_gpu_runtime}" "${CMAKE_BINARY_DIR}/gpu/onnxruntime-dml.dll" COPYONLY)
  configure_file("${_gpu_directml}" "${CMAKE_BINARY_DIR}/gpu/DirectML.dll" COPYONLY)
  install(FILES "${_gpu_runtime}" DESTINATION gpu RENAME onnxruntime-dml.dll)
  install(FILES "${_gpu_directml}" DESTINATION gpu)
  install(FILES "${PicLocateOrtDml_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME ONNX-Runtime-DirectML-LICENSE.txt)
  install(FILES "${PicLocateOrtDml_SOURCE_DIR}/ThirdPartyNotices.txt" DESTINATION licenses RENAME ONNX-Runtime-DirectML-ThirdPartyNotices.txt)
  install(FILES "${PicLocateDirectML_SOURCE_DIR}/LICENSE.txt" DESTINATION licenses RENAME DirectML-LICENSE.txt)
  install(FILES "${PicLocateDirectML_SOURCE_DIR}/LICENSE-CODE.txt" DESTINATION licenses RENAME DirectML-LICENSE-CODE.txt)
  install(FILES "${PicLocateDirectML_SOURCE_DIR}/ThirdPartyNotices.txt" DESTINATION licenses RENAME DirectML-ThirdPartyNotices.txt)
endif()
