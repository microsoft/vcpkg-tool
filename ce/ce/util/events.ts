// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { EventEmitter as eventemitter } from 'ee-ts';
import { EventEmitter } from 'events';
import { Stream } from 'stream';
import { promisify } from 'util';

/**
 * Creates a promise that resolves after a delay
 *
 * @param delayMS the length of time to delay in milliseconds.
 */
export function delay(delayMS: number): Promise<void> {
  return new Promise<void>(res => setTimeout(res, delayMS));
}

export type Emitter<T extends (EventEmitter | eventemitter)> = Pick<T, 'on' | 'off'>;

/**
 * This can proxy event emitters to other sources.
 * Used with an intersect() call to get a promise that has events.
*/
export class EventForwarder<UnionOfEmiters extends (EventEmitter | eventemitter)> implements Emitter<UnionOfEmiters> {
  #emitters = new Array<UnionOfEmiters>();
  #subscriptions = new Array<[string, any]>();

  /**
   * @internal
   *
   * registers the actual event emitter with the forwarder.
   *
   * if events were subscribed to before the emitter is registered, we're going
   * to forward on those subscriptions now.
  */
  register(emitter: UnionOfEmiters) {
    for (const [event, listener] of this.#subscriptions) {
      emitter.on(event, listener);
    }
    this.#emitters.push(emitter);
  }

  /**
   * Lets our forwarder pass on events to subscribe to
   *
   * @remarks we're going to cache these subscriptions, since if the consumer starts subscribing before we
   *          actually register the emitter, we'll have to subscribe at registration time.
   *
   * @param event the event to subscribe to
   * @param listener the callback for the listener
   */
  on(event: any, listener: any) {
    this.#subscriptions.push([event, listener]);
    for (const emitter of this.#emitters) {
      emitter.on(event, listener);
    }
    return <any>this;
  }

  off(event: any, listener: any) {
    for (const emitter of this.#emitters) {
      emitter.off(event, listener);
    }
    return <any>this;
  }
}
/**
 * creates a awaitable promise for a given event.
 * @param eventEmitter the event emitter
 * @param event the event name
 */
export function async(eventEmitter: EventEmitter, event: string | symbol) {
  return promisify(eventEmitter.once)(event);
}

export function completed(stream: Stream): Promise<void> {
  return new Promise((resolve, reject) => {
    stream.once('end', resolve);
    stream.once('error', reject);
  });
}

const ignore = new Set([
  'constructor',
  '__defineGetter__',
  '__defineSetter__',
  'hasOwnProperty',
  '__lookupGetter__',
  '__lookupSetter__',
  'isPrototypeOf',
  'propertyIsEnumerable',
  'toString',
  'valueOf',
  'toLocaleString'
]);

function getMethods<T>(obj: T) {
  const properties = new Set<string>();
  let current = obj;
  do {
    Object.getOwnPropertyNames(current).map(item => properties.add(item));
  } while ((current = Object.getPrototypeOf(current)));
  return [...properties].filter(item => (!ignore.has(item)) && typeof (<any>obj)[item] === 'function');
}

export class ExtendedEmitter<T> extends eventemitter<T> {
  subscribe(listener?: Partial<T>) {
    if (listener) {
      for (const each of getMethods(listener)) {
        this.on(<any>each, (<any>listener)[each]);
      }
    }
  }
  unsubscribe(listener?: Partial<T>) {
    if (listener) {
      for (const each of getMethods(listener)) {
        this.off(<any>each, (<any>listener)[each]);
      }
    }
  }
}