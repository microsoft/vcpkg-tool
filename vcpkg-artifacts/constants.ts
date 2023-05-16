// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

export const undoVariableName = 'Z_VCPKG_UNDO';
export const postscriptVariable = 'Z_VCPKG_POSTSCRIPT';
export const latestVersion = '*';
export const vcpkgDownloadVariable = 'VCPKG_DOWNLOADS';
export const manifestName = 'vcpkg.json';
export const configurationName = 'vcpkg-configuration.json';
export const registryIndexFile = 'index.yaml';

export const defaultConfig =
  `{
  "registries": [
    {
      "kind": "artifact",
      "name": "microsoft",
      "location": "https://github.com/microsoft/vcpkg-ce-catalog/archive/refs/heads/main.zip"
    },
    {
      "kind": "artifact",
      "name": "cmsis",
      "location": "https://github.com/Open-CMSIS-Pack/vcpkg-ce-registry/archive/refs/heads/main.zip"
    }
  ]
}
`;
