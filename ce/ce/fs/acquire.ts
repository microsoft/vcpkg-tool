// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { pipeline as origPipeline } from 'stream';
import { promisify } from 'util';
import { i } from '../i18n';
import { AcquireEvents } from '../interfaces/events';
import { Session } from '../session';
import { Credentials } from '../util/credentials';
import { ExtendedEmitter } from '../util/events';
import { RemoteFileUnavailable } from '../util/exceptions';
import { Algorithm, Hash } from '../util/hash';
import { Uri } from '../util/uri';
import { get, getStream, RemoteFile, resolveRedirect } from './https';
import { ProgressTrackingStream } from './streams';

const pipeline = promisify(origPipeline);

const size32K = 1 << 15;
const size64K = 1 << 16;

export interface AcquireOptions extends Hash {
  /** force a redownload even if it's in cache */
  force?: boolean;
  credentials?: Credentials;
}

export async function acquireArtifactFile(session: Session, uris: Array<Uri>, outputFilename: string, events: Partial<AcquireEvents>, options?: AcquireOptions) {
  await session.cache.createDirectory();
  const outputFile = session.cache.join(outputFilename);
  session.channels.debug(`Acquire file '${outputFilename}' from [${uris.map(each => each.toString()).join(',')}]`);

  if (options?.algorithm && options?.value) {
    session.channels.debug(`We have a hash: ${options.algorithm}/${options.value}`);

    // if we have hash data, check to see if the output file is good.
    if (await outputFile.isFile()) {
      session.channels.debug(`There is an output file already, verifying: ${outputFile.fsPath}`);

      if (await outputFile.hashValid(events, options)) {
        session.channels.debug(`Cached file matched hash: ${outputFile.fsPath}`);
        return outputFile;
      }
    }
  }

  // is the file present on a local filesystem?
  for (const uri of uris) {
    if (uri.isLocal) {
      // we have a local file

      if (options?.algorithm && options?.value) {
        // we have a hash.
        // is it valid?
        if (await uri.hashValid(events, options)) {
          session.channels.debug(`Local file matched hash: ${uri.fsPath}`);
          return uri;
        }
      } else if (await uri.exists()) {
        // we don't have a hash, but the file is local, and it exists.
        // we have to return it
        session.channels.debug(`Using local file (no hash, unable to verify): ${uri.fsPath}`);
        return uri;
      }
      // do we have a filename
    }
  }

  // we don't have a local file
  // https is all that we know at the moment.
  const webUris = uris.where(each => each.isHttps);
  if (webUris.length === 0) {
    // wait, no web uris?
    throw new RemoteFileUnavailable(uris);
  }

  return https(session, webUris, outputFilename, events, options);
}

/** */
async function https(session: Session, uris: Array<Uri>, outputFilename: string, events: Partial<AcquireEvents>, options?: AcquireOptions) {
  const ee = new ExtendedEmitter<AcquireEvents>();
  ee.subscribe(events);
  session.channels.debug(`Attempting to download file '${outputFilename}' from [${uris.map(each => each.toString()).join(',')}]`);

  let resumeAtOffset = 0;
  await session.cache.createDirectory();
  const outputFile = session.cache.join(outputFilename);

  if (options?.force) {
    session.channels.debug(`Acquire '${outputFilename}': force specified, forcing download`);
    // is force specified; delete the current file
    await outputFile.delete();
  }

  // start this peeking at the target uris.
  session.channels.debug(`Acquire '${outputFilename}': checking remote connections`);
  const locations = new RemoteFile(uris, { credentials: options?.credentials });
  let url: Uri | undefined;

  // is there a file in the cache
  if (await outputFile.exists()) {
    session.channels.debug(`Acquire '${outputFilename}': local file exists`);
    if (options?.algorithm) {
      // does it match a hash that we have?
      if (await outputFile.hashValid(events, options)) {
        session.channels.debug(`Acquire '${outputFilename}': local file hash matches metdata`);
        // yes it does. let's just return done.
        return outputFile;
      }
    }

    // it doesn't match a known hash.
    const contentLength = await locations.contentLength;
    session.channels.debug(`Acquire '${outputFilename}': remote connection info is back`);
    const onDiskSize = await outputFile.size();
    if (!await locations.availableLocation) {
      if (locations.failures.all(each => each.code === 404)) {
        let msg = i`Unable to download file`;
        if (options?.credentials) {
          msg += (i` - It could be that your authentication credentials are not correct`);
        }

        session.channels.error(msg);
        throw new RemoteFileUnavailable(uris);
      }
    }
    // first, make sure that there is a remote that is accessible.
    strict.ok(!!await locations.availableLocation, `Requested file ${outputFilename} has no accessible locations ${uris.map(each => each.toString()).join(',')}`);

    url = await locations.resumableLocation;
    // ok, does it support resume?
    if (url) {
      // yes, let's check what the size is expected to be.

      if (!options?.algorithm) {

        if (contentLength === onDiskSize) {
          session.channels.debug(`Acquire '${outputFilename}': on disk file matches length of remote file`);
          const algorithm = <Algorithm>(await locations.algorithm);
          const value = await locations.hash;
          session.channels.debug(`Acquire '${outputFilename}': remote alg/hash: '${algorithm}'/'${value}`);
          if (algorithm && value && outputFile.hashValid(events, { algorithm, value, ...options })) {
            session.channels.debug(`Acquire '${outputFilename}': on disk file hash matches the server hash`);
            // so *we* don't have the hash, but ... if the server has a hash, we could see if what we have is what they have?
            // it does match what the server has.
            // I call this an win.
            return outputFile;
          }

          // we don't have a hash, or what we have doesn't match.
          // maybe we will get a match below (or resume)
        }
      }

      if (onDiskSize > size64K) {
        // it's bigger than 64k. Good. otherwise, we're just wasting time.

        // so, how big is the remote
        if (contentLength >= onDiskSize) {
          session.channels.debug(`Acquire '${outputFilename}': local file length is less than or equal to remote file length`);
          // looks like there could be more remotely than we have.
          // lets compare the first 32k and the last 32k of what we have
          // against what they have and see if they match.
          const top = (await get(url, { start: 0, end: size32K - 1, credentials: options?.credentials })).rawBody;
          const bottom = (await get(url, { start: onDiskSize - size32K, end: onDiskSize - 1, credentials: options?.credentials })).rawBody;

          const onDiskTop = await outputFile.readBlock(0, size32K - 1);
          const onDiskBottom = await outputFile.readBlock(onDiskSize - size32K, onDiskSize - 1);

          if (top.compare(onDiskTop) === 0 && bottom.compare(onDiskBottom) === 0) {
            session.channels.debug(`Acquire '${outputFilename}': first/last blocks are equal`);
            // the start and end of what we have does match what they have.
            // is this file the same size?
            if (contentLength === onDiskSize) {
              // same file size, front and back match, let's accept this. begrudgingly
              session.channels.debug(`Acquire '${outputFilename}': file size is identical. keeping this one`);
              return outputFile;
            }
            // looks like we can continue from here.
            session.channels.debug(`Acquire '${outputFilename}': ok to resume`);
            resumeAtOffset = onDiskSize;
          }
        }
      }
    }
  }

  if (resumeAtOffset === 0) {
    // clearly we mean to not resume. clean any existing file.
    session.channels.debug(`Acquire '${outputFilename}': not resuming file, full download`);
    await outputFile.delete();
  }

  url = url || await locations.availableLocation;
  strict.ok(!!url, `Requested file ${outputFilename} has no accessible locations ${uris.map(each => each.toString()).join(',')}`);
  session.channels.debug(`Acquire '${outputFilename}': initiating download`);
  const length = await locations.contentLength;

  const inputStream = getStream(url, { start: resumeAtOffset, end: length > 0 ? length : undefined, credentials: options?.credentials });
  let progressStream;
  if (length > 0) {
    progressStream = new ProgressTrackingStream(resumeAtOffset, length);
    progressStream.on('progress', (filePercentage) => ee.emit('download', outputFilename, filePercentage));
  }
  const outputStream = await outputFile.writeStream({ append: true });
  ee.emit('download', outputFilename, 0);

  // whoooosh. write out the file
  if (progressStream) {
    await pipeline(inputStream, progressStream, outputStream);
  } else {
    await pipeline(inputStream, outputStream);
  }

  // we've downloaded the file, let's see if it matches the hash we have.
  if (options?.algorithm) {
    session.channels.debug(`Acquire '${outputFilename}': checking downloaded file hash`);
    // does it match the hash that we have?
    if (!await outputFile.hashValid(events, options)) {
      await outputFile.delete();
      throw new Error(i`Downloaded file '${outputFile.fsPath}' did not have the correct hash (${options.algorithm}: ${options.value}) `);
    }
    session.channels.debug(`Acquire '${outputFilename}': downloaded file hash matches specified hash`);
  }

  session.channels.debug(`Acquire '${outputFilename}': downloading file successful`);
  ee.emit('download', outputFilename, 1000);
  ee.emit('complete');
  return outputFile;
}

export async function resolveNugetUrl(session: Session, pkg: string) {
  const [, name, version] = pkg.match(/^(.*)\/(.*)$/) ?? [];
  strict.ok(version, i`package reference '${pkg}' is not a valid nuget package reference ({name}/{version})`);

  // let's resolve the redirect first, since nuget servers don't like us getting HEAD data on the targets via a redirect.
  // even if this wasn't the case, this is lower cost now rather than later.
  const url = await resolveRedirect(session.parseUri(`https://www.nuget.org/api/v2/package/${name}/${version}`));

  session.channels.debug(`Resolving nuget package for '${pkg}' to '${url}'`);
  return url;
}

export async function acquireNugetFile(session: Session, pkg: string, outputFilename: string, events: Partial<AcquireEvents>, options?: AcquireOptions): Promise<Uri> {
  return https(session, [await resolveNugetUrl(session, pkg)], outputFilename, events, options);
}

