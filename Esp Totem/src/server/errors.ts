/** Erro esperado: a mensagem e mostrada ao usuario. */
export class AppError extends Error {
  statusCode: number;

  constructor(message: string, statusCode = 422) {
    super(message);
    this.statusCode = statusCode;
  }
}
