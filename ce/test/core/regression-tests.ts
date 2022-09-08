// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Session } from '@microsoft/vcpkg-ce/dist/session';
import { SuiteLocal } from './SuiteLocal';

async function testRegistry(session: Session, sha: string) {
  const uri = `https://github.com/microsoft/vcpkg-ce-catalog/archive/${sha}.zip`;
  await session.registryDatabase.loadRegistry(session, session.fileSystem.parse(uri));
}

describe('Regressions', () => {
  const local = new SuiteLocal();

  after(local.after.bind(local));

  // These 2 registry loads ensure that we can process both the 'old' and 'new' index.yaml files
  // regression discovered in https://github.com/microsoft/vcpkg-ce-catalog/pull/33

  it('Loads 2ffbc04d6856a1d03c5de0ab94404f90636f7855 registry', async () => {
    await testRegistry(local.session, '2ffbc04d6856a1d03c5de0ab94404f90636f7855');
  });

  it('Loads d471612be63b2fb506ab5f47122da460f5aa4d30 registry', async () => {
    await testRegistry(local.session, 'd471612be63b2fb506ab5f47122da460f5aa4d30');
  });
});
