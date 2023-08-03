// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { SuiteLocal } from './SuiteLocal';

describe('Regressions', () => {
  const local = new SuiteLocal();

  after(local.after.bind(local));

  // These 2 registry loads ensure that we can process both the 'old' and 'new' index.yaml files
  // regression discovered in https://github.com/microsoft/vcpkg-ce-catalog/pull/33

  it('Loads 2ffbc04d6856a1d03c5de0ab94404f90636f7855 registry', async () => {
    await local.session.registryDatabase.loadRegistry(local.session,
      local.resourcesFolderUri.join('vcpkg-ce-catalog-2ffbc04d6856a1d03c5de0ab94404f90636f7855'));
  });

  it('Loads d471612be63b2fb506ab5f47122da460f5aa4d30 registry', async () => {
    await local.session.registryDatabase.loadRegistry(local.session,
      local.resourcesFolderUri.join('vcpkg-ce-catalog-d471612be63b2fb506ab5f47122da460f5aa4d30'));
  });
});
