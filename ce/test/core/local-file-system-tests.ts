// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { FileType } from '@microsoft/vcpkg-ce/dist/fs/filesystem';
import { hash } from '@microsoft/vcpkg-ce/dist/util/hash';
import { strict } from 'assert';
import { pipeline as origPipeline, Writable } from 'stream';
import { promisify } from 'util';
import { SuiteLocal } from './SuiteLocal';

const pipeline = promisify(origPipeline);

function writeAsync(writable: Writable, chunk: Buffer): Promise<void> {
  return new Promise((resolve, reject) => {
    if (writable.write(chunk, (error: Error | null | undefined) => {
      // callback gave us an error.
      if (error) {
        reject(error);
      }
    })) {
      // returned true, we're good to go.
      resolve();
    } else {
      // returned false
      // we were told to wait for it to drain.
      writable.once('drain', resolve);
      writable.once('error', reject);
    }
  });
}

describe('LocalFileSystemTests', () => {
  const local = new SuiteLocal();
  const fs = local.fs;

  after(local.after.bind(local));
  it('create/delete folder', async () => {

    const tmp = local.tempFolderUri;

    // create a path to a folder
    const someFolder = tmp.join('someFolder');

    // create the directory
    await fs.createDirectory(someFolder);

    // is there a directory there?
    strict.ok(await fs.isDirectory(someFolder), `the directory ${someFolder.fsPath} should exist`);

    // delete it
    await fs.delete(someFolder, { recursive: true });

    // make sure it's gone!
    strict.ok(!(await fs.isDirectory(someFolder)), `the directory ${someFolder.fsPath} should not exist`);

  });

  it('create/read file', async () => {
    const tmp = local.tempFolderUri;

    const file = tmp.join('hello.txt');
    const expectedText = 'hello world';
    const expectedBuffer = Buffer.from(expectedText, 'utf8');

    await fs.writeFile(file, expectedBuffer);

    // is there a file there?
    strict.ok(await fs.isFile(file), `the file ${file.fsPath} is not present`);

    // read it back
    const actualBuffer = await fs.readFile(file);
    strict.deepEqual(expectedBuffer, actualBuffer, 'contents should be the same');
    const actualText = actualBuffer.toString();
    strict.equal(expectedText, actualText, 'text should be equal too');

  });

  it('readDirectory', async () => {
    const tmp = local.tempFolderUri;
    const thisFolder = fs.file(__dirname);

    // look in the current folder
    const files = await fs.readDirectory(thisFolder);

    // find this file
    const found = files.find(each => each[0].fsPath.indexOf('local-file-system') > -1);

    // should be a file, right?
    strict.ok(found?.[1] && FileType.File, `${__filename} should be a path`);

  });

  it('read/write stream', async () => {
    const tmp = local.tempFolderUri;

    const thisFile = fs.file(__filename);
    const outputFile = tmp.join('output.txt');

    const outStream = await fs.writeStream(outputFile);
    const outStreamDone = new Promise<void>((resolve, reject) => {
      outStream.once('close', resolve);
      outStream.once('error', reject);
    });

    let text = '';
    // you can iterate thru a stream with 'for await' without casting because I forced the return type to be AsnycIterable<Buffer>
    for await (const chunk of await fs.readStream(thisFile)) {
      text += chunk.toString('utf8');
      await writeAsync(outStream, chunk);
    }
    // close the stream once we're done.
    outStream.end();

    await outStreamDone;

    strict.equal((await fs.stat(outputFile)).size, (await fs.stat(thisFile)).size, 'outputFile should be the same length as the input file');
    strict.equal((await fs.stat(thisFile)).size, text.length, 'buffer should be the same size as the input file');
  });

  it('calculate hashes', async () => {
    const tmp = local.tempFolderUri;
    const path = local.rootFolderUri.join('resources', 'small-file.txt');

    strict.equal(await hash(await fs.readStream(path), path, 0, 'sha256', {}), '9cfed8b9e45f47e735098c399fb523755e4e993ac64d81171c93efbb523a57e6', 'hash should match');
    strict.equal(await hash(await fs.readStream(path), path, 0, 'sha384', {}), '8168d029154548a4e1dd5212b722b03d6220f212f8974f6bd45e71715b13945e343c9d1097f8e393db22c8a07d8cf6f6', 'hash should match');
    strict.equal(await hash(await fs.readStream(path), path, 0, 'sha512', {}), '1bacd5dd190731b5c3d2a2ad61142b4054137d6adff5fb085543dcdede77e4a1446225ca31b2f4699b0cda4534e91ea372cf8d73816df3577e38700c299eab5e', 'hash should match');
  });

  it('reads blocks via open', async () => {
    const file = local.rootFolderUri.join('resources', 'small-file.txt');
    const handle = await file.openFile();
    let bytesRead = 0;
    for await (const chunk of handle.readStream(0, 3)) {
      bytesRead += chunk.length;
      strict.equal(chunk.length, 4, 'chunk should be 4 bytes long');
      strict.equal(chunk.toString('utf-8'), 'this', 'chunk should be a word');
    }
    strict.equal(bytesRead, 4, 'Stream should read some bytes');

    bytesRead = 0;
    // should be able to read that same chunk again.
    for await (const chunk of handle.readStream(0, 3)) {
      bytesRead += chunk.length;
      strict.equal(chunk.length, 4, 'chunk should be 4 bytes long');
      strict.equal(chunk.toString('utf-8'), 'this', 'chunk should be a word');
    }
    strict.equal(bytesRead, 4, 'Stream should read some bytes');

    bytesRead = 0;
    for await (const chunk of handle.readStream()) {
      bytesRead += chunk.length;
      strict.equal(chunk.byteLength, 23, 'chunk should be 23 bytes long');
      strict.equal(chunk.toString('utf-8'), 'this is a small file.\n\n', 'File contents should equal known result');
    }
    strict.equal(bytesRead, 23, 'Stream should read some bytes');

    await handle.close();


  });
  it('reads blocks via open in a large file', async () => {
    const file = local.rootFolderUri.join('resources', 'large-file.txt');
    const handle = await file.openFile();
    let bytesRead = 0;
    for await (const chunk of handle.readStream()) {
      if (bytesRead === 0) {
        strict.equal(chunk.length, 32768, 'first chunk should be 32768 bytes long');
      }
      else {
        strict.equal(chunk.length, 4134, 'second chunk should be 4134 bytes long');
      }
      bytesRead += chunk.length;
    }
    strict.equal(bytesRead, 36902, 'Stream should read some bytes');

    await handle.close();
  });

  it('read/write stream with pipe ', async () => {
    const tmp = local.tempFolderUri;

    const thisFile = fs.file(__filename);
    const thisFileText = (await fs.readFile(thisFile)).toString();
    const outputFile = tmp.join('output2.txt');

    const inputStream = await fs.readStream(thisFile);
    const outStream = await fs.writeStream(outputFile);
    await pipeline(inputStream, outStream);

    strict.ok(fs.isFile(outputFile), `there should be a file at ${outputFile.fsPath}`);

    const outFileText = (await fs.readFile(outputFile)).toString();
    strict.equal(outFileText, thisFileText);

    // this will throw if it fails.
    await fs.delete(outputFile);

    // make sure it's gone!
    strict.ok(!(await fs.isFile(outputFile)), `the file ${outputFile.fsPath} should not exist`);
  });

  it('can copy files', async () => {
    // now copy the files from the test folder
    const files = await local.fs.copy(local.rootFolderUri, local.session.homeFolder.join('junk'));
    strict.ok(files > 3000, `There should be at least 3000 files copied. Only copied ${files}`);
  });
});
