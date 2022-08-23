// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Activation } from '@microsoft/vcpkg-ce/dist/artifacts/activation';
import { strict } from 'assert';
import { platform } from 'os';
import { SuiteLocal } from './SuiteLocal';

describe('MSBuild Generator', () => {
  const local = new SuiteLocal();

  after(local.after.bind(local));

  it('Generates roots with a trailing slash', () => {
    const activation = new Activation(local.session);
    const expectedPosix = 'c:/tmp';
    const expected = (platform() === 'win32') ? expectedPosix.replaceAll('/', '\\') : expectedPosix;
    strict.equal(activation.msBuildProcessPropertyValue('$root$', local.fs.file('c:/tmp')), expected);
    strict.equal(activation.msBuildProcessPropertyValue('$root$', local.fs.file('c:/tmp/')), expected);
  });

  it('Generates locations in order', () => {
    const activation = new Activation(local.session);

    // Note that only "addMSBuildProperty" has an effect on the output for now but that we'll probably
    // need to respond to the others in the future.
    (<Array<[string, string | Array<string>]>>[
      ['z', 'zse&tting'],
      ['a', 'ase<tting'],
      ['c', 'csetting'],
      ['b', 'bsetting'],
      ['prop', ['first', 'seco>nd', 'third']]
    ]).forEach(([key, value]) => activation.addProperty(key, typeof value === 'string' ? [value] : value));

    activation.addLocation('somepath', local.fs.file('c:/tmp'));
    activation.addPath('include', [local.fs.file('c:/tmp'), local.fs.file('c:/tmp2')]);
    activation.addDefine('POSIX_ME_HARDER', '1');

    const fileWithNoSlash = local.fs.file('c:/tmp');
    const fileWithSlash = local.fs.file('c:/tmp/');
    activation.addMSBuildProperty('a', '$(a);fir$root$st', fileWithNoSlash);
    activation.addMSBuildProperty('a', '$(a);second', fileWithNoSlash);
    activation.addMSBuildProperty('a', '$(a);$root$hello', fileWithNoSlash);
    activation.addMSBuildProperty('b', '$(x);first', fileWithSlash);
    activation.addMSBuildProperty('b', '$(b);se$root$cond', fileWithSlash);
    activation.addMSBuildProperty('a', '$(a);third', fileWithNoSlash);
    activation.addMSBuildProperty('b', 'third', fileWithSlash);
    activation.addMSBuildProperty('b', '$(b);$root$world', fileWithSlash);

    const expectedPosix = `<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <a>$(a);firc:/tmpst</a>
    <a>$(a);second</a>
    <a>$(a);c:/tmphello</a>
    <b>$(x);first</b>
    <b>$(b);sec:/tmpcond</b>
    <a>$(a);third</a>
    <b>third</b>
    <b>$(b);c:/tmpworld</b>
  </PropertyGroup>
</Project>`;

    const expected = (platform() === 'win32')
      ? expectedPosix.replaceAll('c:/tmp', 'c:\\tmp').replaceAll('c:/', 'c:\\')
      : expectedPosix;
    strict.equal(activation.generateMSBuild(), expected);
  });
});
