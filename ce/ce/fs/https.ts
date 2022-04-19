// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { default as got, Headers, HTTPError, Response } from 'got';
import { Credentials } from '../util/credentials';
import { anyWhere } from '../util/promise';
import { Uri } from '../util/uri';

/**
 * Resolves an HTTPS GET redirect by doing the GET, grabbing the redirects and then cancelling the rest of the request
 * @param location the URL to get the final location of
 */
export async function resolveRedirect(location: Uri) {
  let finalUrl = location;

  const stream = got.get(location.toUrl(), { timeout: 15000, isStream: true });

  // when the response comes thru, we can grab the headers & stuff from it
  stream.on('response', (response: Response) => {
    finalUrl = location.fileSystem.parse(response.redirectUrls.last || finalUrl.toString());
  });

  // we have to get at least some data for the response event to trigger.
  for await (const chunk of stream) {
    // but we don't need any of it :D
    break;
  }
  stream.destroy();
  return finalUrl;
}

/**
 * Does an HTTPS HEAD request, and on a 404, tries to do an HTTPS GET and see if we get a redirect, and harvest the headers from that.
 * @param location the target URL
 * @param headers any headers to put in the request.
 */
export async function head(location: Uri, headers: Headers = {}, credentials?: Credentials): Promise<Response<string>> {
  try {
    setCredentials(headers, location, credentials);
    // on a successful HEAD request, do nothing different
    return await got.head(location.toUrl(), { timeout: 15000, headers });
  } catch (E) {
    // O_o
    //
    // So, it turns out that nuget servers (maybe others too?) don't do redirects on HEAD requests,
    // and instead issue a 404.
    // let's retry the request as a GET, and dump it after the first chunk.
    // typically, a HEAD request should see a 300-400msec response time
    // and yes, this does stretch that out to 500-700msec, but whatcha gonna do?
    if (E instanceof HTTPError && E.response.statusCode === 404) {
      try {
        const syntheticResponse = <Response<string>>{};
        const stream = got.get(location.toUrl(), { timeout: 15000, headers, isStream: true });

        // when the response comes thru, we can grab the headers & stuff from it
        stream.on('response', (response: Response) => {
          syntheticResponse.headers = response.headers;
          syntheticResponse.statusCode = response.statusCode;
          syntheticResponse.redirectUrls = response.redirectUrls;
        });

        // we have to get at least some data for the response event to trigger.
        for await (const chunk of stream) {
          // but we don't need any of it :D
          break;
        }
        stream.destroy();
        return syntheticResponse;
      }
      catch {
        // whatever, it didn't work. let the rethrow happen.
      }
    }
    throw E;
  }
}

/** HTTPS Get request, returns a buffer  */
export function get(location: Uri, options?: { start?: number, end?: number, headers?: Headers, credentials?: Credentials }) {
  let headers: Headers | undefined = undefined;
  headers = setRange(headers, options?.start, options?.end);
  headers = setCredentials(headers, location, options?.credentials);

  return got.get(location.toUrl(), { headers });
}

function setRange(headers: Headers | undefined, start?: number, end?: number) {
  if (start !== undefined || end !== undefined) {
    headers = headers || {};
    headers['range'] = `bytes=${start !== undefined ? start : ''}-${end !== undefined ? end : ''}`;
  }
  return headers;
}


function setCredentials(headers: Headers | undefined, target: Uri, credentials?: Credentials) {
  if (credentials) {
    // todo: if we have to add some credential headers, we'd do it here.
    // we've removed github auth support until we actually need such a thing
  }
  return headers;
}

/** HTTPS Get request, returns a stream
 * @internal
*/
export function getStream(location: Uri, options?: { start?: number, end?: number, headers?: Headers, credentials?: Credentials }) {
  let headers: Headers | undefined = options?.headers;
  headers = setRange(headers, options?.start, undefined);
  headers = setCredentials(headers, location, options?.credentials);

  return got.get(location.toUrl(), { isStream: true, retry: 3, headers });
}

export interface Info {
  failed?: boolean;
  location: Uri;
  resumeable: boolean;
  contentLength: number;
  hash?: string;
  algorithm?: string;
}

function digest(headers: Headers) {
  let hash = hashAlgorithm(headers['digest'], 'sha-256');

  // any of the sha* hashes..
  if (hash) {
    return { hash, algorithm: 'sha256' };
  }
  hash = hashAlgorithm(headers['digest'], 'sha-384');
  if (hash) {
    return { hash, algorithm: 'sha384' };
  }
  hash = hashAlgorithm(headers['digest'], 'sha-512');
  if (hash) {
    return { hash, algorithm: 'sha512' };
  }

  // nothing we know about.
  return { hash: undefined, algorithm: undefined };
}

/**
 * RemoteFile is a class that represents a single remote file, but mapped to multiple mirrored URLs
 * on creation, it kicks off HEAD requests to each URL so that we can get hash/digest, length, resumability etc
 *
 * the properties are Promises<> to the results, where it grabs data from the first returning valid query without
 * blocking elsewhere.
 *
*/
export class RemoteFile {
  info: Array<Promise<Info>>;
  constructor(protected locations: Array<Uri>, options?: { credentials?: Credentials }) {
    this.info = locations.map(location => {
      return head(location, setCredentials({
        'want-digest': 'sha-256;q=1, sha-512;q=0.9',
        'accept-encoding': 'identity;q=0', // we need to know the content length without gzip encoding,
      }, location, options?.credentials)).then(data => {
        if (data.statusCode === 200) {
          const { hash, algorithm } = digest(data.headers);
          return {
            location,
            resumeable: data.headers['accept-ranges'] === 'bytes',
            contentLength: Number.parseInt(data.headers['content-length']!) || -1, // -1 means we were not told.
            hash,
            algorithm,
          };
        }
        this.failures.push({
          code: data.statusCode,
          reason: `A non-ok status code was returned: ${data.statusMessage}`
        });
        throw new Error(`A non-ok status code was returned: ${data.statusCode}`);
      }, err => {
        this.failures.push({
          code: err?.response?.statusCode,
          reason: `A non-ok status code was returned: ${err?.response?.statusMessage}`
        });
        throw err;
      });
    });


    // lazy properties (which do not throw on errors.)
    this.availableLocation = Promise.any(this.info).then(success => success.location, fail => undefined);
    this.resumable = anyWhere(this.info, each => each.resumeable).then(success => true, fail => false);
    this.resumableLocation = anyWhere(this.info, each => each.resumeable).then(success => success.location, fail => undefined);
    this.contentLength = anyWhere(this.info, each => !!each.contentLength).then(success => success.contentLength, fail => -2);
    this.hash = anyWhere(this.info, each => !!each.hash).then(success => success.hash, fail => undefined);
    this.algorithm = anyWhere(this.info, each => !!each.algorithm).then(success => success.algorithm, fail => undefined);
  }

  resumable: Promise<boolean>;
  contentLength: Promise<number>;
  hash: Promise<string | undefined>;
  algorithm: Promise<string | undefined>;
  availableLocation: Promise<Uri | undefined>;
  resumableLocation: Promise<Uri | undefined>;
  failures = new Array<{ code: number, reason: string }>();
}

/**
 * Digest/hash in headers are base64 encoded strings.
 * @param data the base64 encoded string
 */
function decode(data?: string): string | undefined {
  return data ? Buffer.from(data, 'base64').toString('hex').toLowerCase() : undefined;
}

/**
 * Get the hash alg/hash from the digest.
 * @param digest the digest header
 * @param algorithm the algorithm we're trying to match
 */
function hashAlgorithm(digest: string | Array<string> | undefined, algorithm: 'sha-256' | 'sha-384' | 'sha-512'): string | undefined {
  for (const each of (digest ? Array.isArray(digest) ? digest : [digest] : [])) {
    if (each.startsWith(algorithm)) {
      return decode(each.substr(8));
    }
  }

  // nothing.
  return undefined;
}
