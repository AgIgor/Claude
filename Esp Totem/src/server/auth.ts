import { createHmac, randomBytes, randomInt, scryptSync, timingSafeEqual } from 'node:crypto';

export type TokenPayload =
  | { kind: 'staff'; storeId: string; userId: string; exp: number }
  | { kind: 'table'; storeId: string; billId: string; exp: number };

const encode = (value: string) => Buffer.from(value).toString('base64url');
const sign = (body: string, secret: string) => createHmac('sha256', secret).update(body).digest('base64url');

export function createToken(payload: TokenPayload, secret: string): string {
  const body = encode(JSON.stringify(payload));
  return `${body}.${sign(body, secret)}`;
}

export function readToken(token: string | undefined, secret: string): TokenPayload | null {
  if (!token) return null;
  const [body, signature, ...rest] = token.split('.');
  if (!body || !signature || rest.length > 0) return null;

  const expected = Buffer.from(sign(body, secret));
  const received = Buffer.from(signature);
  if (expected.length !== received.length || !timingSafeEqual(expected, received)) return null;

  const payload = JSON.parse(Buffer.from(body, 'base64url').toString()) as TokenPayload;
  return payload.exp > Date.now() ? payload : null;
}

export function hashPassword(password: string): string {
  const salt = randomBytes(16);
  return `${salt.toString('hex')}:${scryptSync(password, salt, 32).toString('hex')}`;
}

export function verifyPassword(password: string, stored: string): boolean {
  const [salt, hash] = stored.split(':');
  const expected = Buffer.from(hash, 'hex');
  return timingSafeEqual(expected, scryptSync(password, Buffer.from(salt, 'hex'), expected.length));
}

export const randomPin = () => String(randomInt(0, 10_000)).padStart(4, '0');
export const randomQrToken = () => randomBytes(12).toString('base64url');
