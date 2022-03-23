// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Activation } from '@microsoft/vcpkg-ce/dist/artifacts/activation';
import { strict } from 'assert';
import { platform } from 'os';
import { SuiteLocal } from './SuiteLocal';

describe('MSBuild Generator', () => {
  const local = new SuiteLocal();
  const fs = local.fs;

  after(local.after.bind(local));

  it('Generates locations in order', () => {

    const activation = new Activation(local.session);

    (<Array<[string, string | Array<string>]>>[
      ['z', 'zse&tting'],
      ['a', 'ase<tting'],
      ['c', 'csetting'],
      ['b', 'bsetting'],
      ['prop', ['first', 'seco>nd', 'third']]
    ]).forEach(([key, value]) => activation.properties.set(key, typeof value === 'string' ? [value] : value));

    activation.locations.set('somepath', local.fs.file('c:/tmp'));
    activation.paths.set('include', [local.fs.file('c:/tmp'), local.fs.file('c:/tmp2')]);

    const expected = (platform() === 'win32') ? `<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Label="Locations">
    <somepath>c:\\tmp</somepath>
  </PropertyGroup>
  <PropertyGroup Label="Properties">
    <z>zse&amp;tting</z>
    <a>ase&lt;tting</a>
    <c>csetting</c>
    <b>bsetting</b>
    <prop>first;seco&gt;nd;third</prop>
  </PropertyGroup>
  <PropertyGroup Label="Paths">
    <include>c:\\tmp;c:\\tmp2</include>
  </PropertyGroup>
</Project>` : `<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Label="Locations">
    <somepath>c:/tmp</somepath>
  </PropertyGroup>
  <PropertyGroup Label="Properties">
    <z>zse&amp;tting</z>
    <a>ase&lt;tting</a>
    <c>csetting</c>
    <b>bsetting</b>
    <prop>first;seco&gt;nd;third</prop>
  </PropertyGroup>
  <PropertyGroup Label="Paths">
    <include>c:/tmp;c:/tmp2</include>
  </PropertyGroup>
</Project>` ;

    strict.equal(activation.generateMSBuild([]), expected);
  });
});
