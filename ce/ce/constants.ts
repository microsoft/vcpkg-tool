// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

export const undo = 'Z_VCPKG_UNDO';
export const postscriptVariable = 'Z_VCPKG_POSTSCRIPT';
export const blank = '\n';
export const latestVersion = '*';
export const vcpkgDownloadFolder = 'VCPKG_DOWNLOADS';
export const globalConfigurationFile = 'vcpkg-configuration.global.json';
export const profileNames = ['vcpkg-configuration.json', 'vcpkg-configuration.yaml', 'environment.yaml', 'environment.yml', 'environment.json'];
export const registryIndexFile = 'index.yaml';

export const defaultConfig =
  `{
  "registries": [
    {
      "kind": "artifact",
      "name": "microsoft",
      "location": "https://aka.ms/vcpkg-ce-default"
    }
  ]
}
`;
