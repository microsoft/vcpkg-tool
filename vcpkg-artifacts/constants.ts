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
      "location": "https://aka.ms/vcpkg-ce-default"
    },
    {
      "kind": "artifact",
      "name": "cmsis",
      "location": "https://aka.ms/vcpkg-artifacts-cmsis"
    }
  ]
}
`;
