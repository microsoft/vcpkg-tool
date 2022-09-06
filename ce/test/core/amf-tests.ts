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
    const content = await (await readFile(join(rootFolder(), 'resources', 'sample1.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./sample1.json', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure it is valid json');
    strict.sequenceEqual(doc.validate(), []);

    strict.equal(doc.id, 'sample1', 'name incorrect');
    strict.equal(doc.version, '1.2.3', 'version incorrect');
  });

  it('reads file with nupkg', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'repo', 'sdks', 'microsoft', 'windows.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./windows.json', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure it is valid json');
    strict.sequenceEqual(doc.validate(), []);

    SuiteLocal.log(doc.toJsonString());
  });

  it('load/persist an artifact', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'example-artifact.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./example-artifact.json', content, local.session);

    SuiteLocal.log(doc.toJsonString());
    strict.ok(doc.isFormatValid, 'Ensure it\'s valid');
    for (const each of doc.validate()) {
      SuiteLocal.log(doc.formatVMessage(each));
    }
  });

  it('profile checks', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'sample1.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./sample1.json', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure that it is valid json');
    strict.sequenceEqual(doc.validate(), []);

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

    strict.equal(doc.exports.tools.get('CC'), 'foo/bar/cl.exe', 'should have a value');
    strict.equal(doc.exports.tools.get('CXX'), 'bin/baz/cl.exe', 'should have a value');
    strict.equal(doc.exports.tools.get('Whatever'), 'some/tool/path/foo', 'should have a value');

    doc.exports.tools.delete('CXX');
    strict.equal(doc.exports.tools.keys.length, 2, 'should only have two tools now');

    strict.sequenceEqual(doc.exports.environment.get('test'), ['abc'], 'variables should be an array');
    strict.sequenceEqual(doc.exports.environment.get('cxxflags'), ['foo=bar', 'bar=baz'], 'variables should be an array');

    doc.exports.environment.add('test').add('another value');
    strict.sequenceEqual(doc.exports.environment.get('test'), ['abc', 'another value'], 'variables should be an array of two items now');

    doc.exports.paths.add('bin').add('hello/there');
    strict.deepEqual(doc.exports.paths.get('bin')?.length, 3, 'there should be three paths in bin now');

    strict.sequenceEqual(doc.conditionalDemands.keys, ['windows and arm'], 'should have one conditional demand');
    /*
    const install = doc.get('windows and arm').install[0];

    strict.ok(isNupkg(install), 'the install type should be nupkg');
    strict.equal((install).location, 'floobaloo/1.2.3', 'should have correct location');
*/
    SuiteLocal.log(doc.toString());
  });

  it('read invalid json file', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'errors.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./errors.json', content, local.session);

    strict.equal(doc.isFormatValid, false, 'this document should have errors');
    strict.equal(doc.formatErrors.length, 2, 'This document should have two error');

    strict.equal(doc.id, 'bob', 'name incorrect');
    strict.equal(doc.version, '1.0.2', 'version incorrect');
  });

  it('read empty json file', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'empty.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./empty.json', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure it is valid json');

    const [firstError] = doc.validate();
    strict.equal(doc.formatVMessage(firstError), './empty.json:1:1 FieldMissing, Missing identity \'id\'', 'Should have an error about id');
  });

  it('validation errors', async () => {
    const content = await (await readFile(join(rootFolder(), 'resources', 'validation-errors.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./validation-errors.json', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure it is valid json');

    const validationErrors = Array.from(doc.validate(), (error) => doc.formatVMessage(error));
    strict.equal(validationErrors.length, 7, `Expecting 7 errors, found: ${JSON.stringify(validationErrors, null, 2)}`);
  });
});
