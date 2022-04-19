// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { MetadataFile } from '@microsoft/vcpkg-ce/dist/amf/metadata-file';
import { strict } from 'assert';
import { readFile } from 'fs/promises';
import { join } from 'path';
import * as s from '../sequence-equal';
import { rootFolder, SuiteLocal } from './SuiteLocal';

// forces the global function for sequence equal to be added to strict before this exectues:
s;

// sample test using decorators.
describe('Amf', () => {
  const local = new SuiteLocal();
  const fs = local.fs;

  after(local.after.bind(local));

  it('readProfile', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'sample1.yaml'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./sample1.yaml', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure it is valid yaml');
    strict.ok(doc.isValid, 'Is it valid?');

    strict.equal(doc.info.id, 'sample1', 'identity incorrect');
    strict.equal(doc.info.version, '1.2.3', 'version incorrect');
  });

  it('reads file with nupkg', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'repo', 'sdks', 'microsoft', 'windows.yaml'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./windows.yaml', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure it is valid yaml');
    strict.ok(doc.isValid, 'Is it valid?');

    SuiteLocal.log(doc.content);
  });

  it('load/persist environment.yaml', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'environment.yaml'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./cenvironment.yaml', content, local.session);

    SuiteLocal.log(doc.content);
    for (const each of doc.validationErrors) {
      SuiteLocal.log(each);
    }

    strict.ok(doc.isFormatValid, 'Ensure it\'s valid yaml');
    strict.ok(doc.isValid, 'better be valid!');

    SuiteLocal.log(doc.content);
  });

  it('profile checks', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'sample1.yaml'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./sample1.yaml', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure it\'s valid yaml');
    SuiteLocal.log(doc.validationErrors);
    strict.ok(doc.isValid, 'better be valid!');

    // fixme: validate inputs again.
    // strict.throws(() => doc.info.version = '4.1', 'Setting invalid version should throw');
    // strict.equal(doc.info.version = '4.1.0', '4.1.0', 'Version should set correctly');

    SuiteLocal.log(doc.contacts.get('Bob Smith'));

    strict.sequenceEqual(doc.contacts.get('Bob Smith')!.roles, ['fallguy', 'otherguy'], 'Should return the two roles');
    doc.contacts.get('Bob Smith')!.roles.delete('fallguy');

    strict.sequenceEqual(doc.contacts.get('Bob Smith')!.roles, ['otherguy'], 'Should return the remaining role');

    doc.contacts.get('Bob Smith')!.roles.add('the dude');

    doc.contacts.get('Bob Smith')!.roles.add('the dude'); // shouldn't add this one

    strict.sequenceEqual(doc.contacts.get('Bob Smith')!.roles, ['otherguy', 'the dude'], 'Should return only two roles');

    const k = doc.contacts.add('James Brown');
    SuiteLocal.log(doc.contacts.keys);

    k.email = 'jim@contoso.net';
    SuiteLocal.log(doc.contacts.keys);

    strict.equal(doc.contacts.keys.length, 3, 'Should have 3 contacts');

    doc.contacts.delete('James Brown');


    strict.equal(doc.contacts.keys.length, 2, 'Should have 2 contacts');

    doc.contacts.delete('James Brown'); // this is ok.

    // version can be coerced to be a string (via tostring)
    SuiteLocal.log(doc.requires.get('foo/bar/bin')?.raw);
    strict.equal(doc.requires.get('foo/bar/bin')?.raw == '~2.0.0', true, 'Version must match');

    // can we get the normalized range?
    strict.equal(doc.requires.get('foo/bar/bin')!.range.range, '>=2.0.0 <2.1.0-0', 'The canonical ranges should match');

    // no resolved version means undefined.
    strict.equal(doc.requires.get('foo/bar/bin')!.resolved, undefined, 'Version must match');

    // the setter is actually smart enough, but typescript does not allow heterogeneous accessors (yet! https://github.com/microsoft/TypeScript/issues/2521)
    doc.requires.set('just/a/version', <any>'1.2.3');
    strict.equal(doc.requires.get('just/a/version')!.raw, '1.2.3', 'Should be a static version range');

    // set it with a struct
    doc.requires.set('range/with/resolved', <any>{ range: '1.*', resolved: '1.0.0' });
    strict.equal(doc.requires.get('range/with/resolved')!.raw, '1.* 1.0.0');

    strict.equal(doc.settings.tools.get('CC'), 'foo/bar/cl.exe', 'should have a value');
    strict.equal(doc.settings.tools.get('CXX'), 'bin/baz/cl.exe', 'should have a value');
    strict.equal(doc.settings.tools.get('Whatever'), 'some/tool/path/foo', 'should have a value');

    doc.settings.tools.delete('CXX');
    strict.equal(doc.settings.tools.keys.length, 2, 'should only have two tools now');

    strict.sequenceEqual(doc.settings.variables.get('test'), ['abc'], 'variables should be an array');
    strict.sequenceEqual(doc.settings.variables.get('cxxflags'), ['foo=bar', 'bar=baz'], 'variables should be an array');

    doc.settings.variables.add('test').add('another value');
    strict.sequenceEqual(doc.settings.variables.get('test'), ['abc', 'another value'], 'variables should be an array of two items now');

    doc.settings.paths.add('bin').add('hello/there');
    strict.deepEqual(doc.settings.paths.get('bin')?.length, 3, 'there should be three paths in bin now');

    strict.sequenceEqual(doc.conditionalDemands.keys, ['windows and arm'], 'should have one conditional demand');
    /*
    const install = doc.get('windows and arm').install[0];

    strict.ok(isNupkg(install), 'the install type should be nupkg');
    strict.equal((install).location, 'floobaloo/1.2.3', 'should have correct location');
*/
    SuiteLocal.log(doc.toString());
  });

  it('read invalid yaml file', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'errors.yaml'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./errors.yaml', content, local.session);

    strict.equal(doc.isFormatValid, false, 'this document should have errors');
    strict.equal(doc.formatErrors.length, 2, 'This document should have two error');

    strict.equal(doc.info.id, 'bob', 'identity incorrect');
    strict.equal(doc.info.version, '1.0.2', 'version incorrect');
  });

  it('read empty yaml file', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'empty.yaml'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./empty.yaml', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure it is valid yaml');

    strict.equal(doc.isValid, false, 'Should have some validation errors');
    strict.equal(doc.validationErrors[0], './empty.yaml:1:1 SectionMessing, Missing section \'info\'', 'Should have an error about info');
  });

  it('validation errors', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'validation-errors.yaml'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./validation-errors.yaml', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure it is valid yaml');

    SuiteLocal.log(doc.validationErrors);
    strict.equal(doc.validationErrors.length, 2, `Expecting two errors, found: ${JSON.stringify(doc.validationErrors, null, 2)}`);
  });
});
