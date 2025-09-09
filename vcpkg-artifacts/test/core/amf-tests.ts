// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { readFile } from 'fs/promises';
import { join } from 'path';
import { MetadataFile } from '../../amf/metadata-file';
import { strictSequenceEqual } from '../sequence-equal';
import { SuiteLocal } from './SuiteLocal';

// sample test using decorators.
describe('Amf', () => {
  const local = new SuiteLocal();

  after(local.after.bind(local));

  it('readProfile', async () => {
    const content = await (await readFile(join(local.resourcesFolder, 'sample1.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./sample1.json', content, local.session);

    strict.ok(doc.isFormatValid);
    strictSequenceEqual(doc.validate(), []);

    strict.equal(doc.id, 'sample1', 'name incorrect');
    strict.equal(doc.version, '1.2.3', 'version incorrect');
  });

  it('reads file with nupkg', async () => {
    const content = await (await readFile(join(local.resourcesFolder, 'repo', 'sdks', 'microsoft', 'windows.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./windows.json', content, local.session);

    strict.ok(doc.isFormatValid);
    strictSequenceEqual(doc.validate(), []);
  });

  it('load/persist an artifact', async () => {
    const content = await (await readFile(join(local.resourcesFolder, 'example-artifact.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./example-artifact.json', content, local.session);

    strict.ok(doc.isFormatValid);
    strictSequenceEqual(doc.validate(), []);
  });

  it('profile checks', async () => {
    const content = await (await readFile(join(local.resourcesFolder, 'sample1.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./sample1.json', content, local.session);

    strict.ok(doc.isFormatValid, 'Ensure that it is valid json');
    strictSequenceEqual(doc.validate(), []);

    strictSequenceEqual(doc.contacts.get('Bob Smith')!.roles, ['fallguy', 'otherguy'], 'Should return the two roles');
    doc.contacts.get('Bob Smith')!.roles.delete('fallguy');

    strictSequenceEqual(doc.contacts.get('Bob Smith')!.roles, ['otherguy'], 'Should return the remaining role');

    doc.contacts.get('Bob Smith')!.roles.add('the dude');

    doc.contacts.get('Bob Smith')!.roles.add('the dude'); // shouldn't add this one

    strictSequenceEqual(doc.contacts.get('Bob Smith')!.roles, ['otherguy', 'the dude'], 'Should return only two roles');

    const k = doc.contacts.add('James Brown');

    k.email = 'jim@contoso.net';

    strict.equal(doc.contacts.keys.length, 3, 'Should have 3 contacts');

    doc.contacts.delete('James Brown');


    strict.equal(doc.contacts.keys.length, 2, 'Should have 2 contacts');

    doc.contacts.delete('James Brown'); // this is ok.

    // version can be coerced to be a string (via tostring)
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

    strictSequenceEqual(doc.exports.environment.get('test'), ['abc'], 'variables should be an array');
    strictSequenceEqual(doc.exports.environment.get('cxxflags'), ['foo=bar', 'bar=baz'], 'variables should be an array');

    doc.exports.environment.add('test').add('another value');
    strictSequenceEqual(doc.exports.environment.get('test'), ['abc', 'another value'], 'variables should be an array of two items now');

    doc.exports.paths.add('bin').add('hello/there');
    strict.deepEqual(doc.exports.paths.get('bin')?.length, 3, 'there should be three paths in bin now');

    strictSequenceEqual(doc.conditionalDemands.keys, ['windows and arm'], 'should have one conditional demand');
  });

  it('read invalid json file', async () => {
    const content = await (await readFile(join(local.resourcesFolder, 'errors.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./errors.json', content, local.session);

    strict.equal(doc.isFormatValid, false, 'this document should have errors');
    strict.equal(doc.formatErrors.length, 2, 'This document should have two error');

    strict.equal(doc.id, 'bob', 'name incorrect');
    strict.equal(doc.version, '1.0.2', 'version incorrect');
  });

  it('read empty json file', async () => {
    const content = await (await readFile(join(local.resourcesFolder, 'empty.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./empty.json', content, local.session);

    strict.ok(doc.isFormatValid);

    const validationErrors = Array.from(doc.validate(), (error) => doc.formatVMessage(error));
    strictSequenceEqual(validationErrors, [
      './empty.json:1:1 FieldMissing, Missing identity \'id\'',
      './empty.json:1:1 FieldMissing, Missing version \'version\''
    ]);
    const [firstError] = doc.validate();
    strict.equal(doc.formatVMessage(firstError), './empty.json:1:1 FieldMissing, Missing identity \'id\'', 'Should have an error about id');
  });

  it('validation errors', async () => {
    const content = await (await readFile(join(local.resourcesFolder, 'validation-errors.json'))).toString('utf-8');
    const doc = await MetadataFile.parseConfiguration('./validation-errors.json', content, local.session);

    strict.ok(doc.isFormatValid);

    const validationErrors = Array.from(doc.validate(), (error) => doc.formatVMessage(error));
    strictSequenceEqual(validationErrors,[
      './validation-errors.json:5:15 InvalidChild, Unexpected \'goober )\' found in $',
      './validation-errors.json:8:13 InvalidChild, Unexpected \'goober\' found in $',
      './validation-errors.json:11:13 InvalidChild, Unexpected \'floopy\' found in $',
      './validation-errors.json:12:29 InvalidChild, Unexpected \'windows and target:x64\' found in $',
      './validation-errors.json:3:16 InvalidChild, Unexpected \'nothing\' found in $.info',
      './validation-errors.json:2:11 FieldMissing, Missing identity \'info.id\'',
      './validation-errors.json:2:11 FieldMissing, Missing version \'info.version\''
    ]);
  });
});
