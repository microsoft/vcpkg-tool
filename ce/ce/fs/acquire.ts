// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { i } from '../i18n';
import { DownloadEvents } from '../interfaces/events';
import { Session } from '../session';
import { RemoteFileUnavailable } from '../util/exceptions';
import { Hash } from '../util/hash';
import { Uri } from '../util/uri';
import { vcpkgDownload } from '../vcpkg';

export interface AcquireOptions extends Hash {
  /** force a redownload even if it's in cache */
  force?: boolean;
}

export async function acquireArtifactFile(session: Session, uris: Array<Uri>, outputFilename: string, events: Partial<DownloadEvents>, options?: AcquireOptions) {
  await session.downloads.createDirectory();
  session.channels.debug(`Acquire file '${outputFilename}' from [${uris.map(each => each.toString()).join(',')}]`);

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
async function https(session: Session, uris: Array<Uri>, outputFilename: string, events: Partial<DownloadEvents>, options?: AcquireOptions) {
  session.channels.debug(`Attempting to download file '${outputFilename}' from [${uris.map(each => each.toString()).join(',')}]`);
  const hashAlgorithm = options?.algorithm;
  const outputFile = session.downloads.join(outputFilename);
  if (options?.force) {
    session.channels.debug(`Acquire '${outputFilename}': force specified, forcing download`);
    // is force specified; delete the current file
    await outputFile.delete();
  } else if (hashAlgorithm) {
    // does it match a hash that we have?
    if (await outputFile.hashValid(events, options)) {
      session.channels.debug(`Acquire '${outputFilename}': local file hash matches metdata`);
      // yes it does. let's just return done.
      return outputFile;
    }

    // invalid hash, deleting file
    session.channels.debug(`Acquire '${outputFilename}': local file hash mismatch, redownloading`);
    await outputFile.delete();
  } else if (await outputFile.exists()) {
    session.channels.debug(`Acquire '${outputFilename}': skipped due to existing file, no hash known`);
    session.channels.warning(i`Assuming '${outputFilename}' is correct; supply a hash in the artifact metadata to suppress this message.`);
    return outputFile;
  }

  session.channels.debug(`Acquire '${outputFilename}': checking remote connections`);
  events.downloadStart?.(uris, outputFile.fsPath);
  let sha512 = undefined;
  if (hashAlgorithm == 'sha512') {
    sha512 = options?.value;
  }

  const vcpkgOutput = await vcpkgDownload(session, outputFile, sha512, uris);
  session.channels.debug(`vcpkg said: ${vcpkgOutput}`);

  events.downloadComplete?.();
  // we've downloaded the file, let's see if it matches the hash we have.
  if (hashAlgorithm == 'sha512') {
    // vcpkg took care of it already
    session.channels.debug(`Acquire '${outputFilename}': vcpkg checked SHA512`);
  } else if (hashAlgorithm) {
    session.channels.debug(`Acquire '${outputFilename}': checking downloaded file hash`);
    // does it match the hash that we have?
    if (!await outputFile.hashValid(events, options)) {
      await outputFile.delete();
      throw new Error(i`Downloaded file '${outputFile.fsPath}' did not have the correct hash (${options.algorithm}: ${options.value}) `);
    }

    session.channels.debug(`Acquire '${outputFilename}': downloaded file hash matches specified hash`);
  }

  session.channels.debug(`Acquire '${outputFilename}': downloading file successful`);
  return outputFile;
}

export async function resolveNugetUrl(session: Session, pkg: string) {
  const [, name, version] = pkg.match(/^(.*)\/(.*)$/) ?? [];
  strict.ok(version, i`package reference '${pkg}' is not a valid nuget package reference ({name}/{version})`);

  // let's resolve the redirect first, since nuget servers don't like us getting HEAD data on the targets via a redirect.
  // even if this wasn't the case, this is lower cost now rather than later.
  return session.fileSystem.parseUri(`https://www.nuget.org/api/v2/package/${name}/${version}`);
}

export async function acquireNugetFile(session: Session, pkg: string, outputFilename: string, events: Partial<DownloadEvents>, options?: AcquireOptions): Promise<Uri> {
  return https(session, [await resolveNugetUrl(session, pkg)], outputFilename, events, options);
}
