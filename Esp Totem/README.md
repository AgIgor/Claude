# Esp Totem

Sistema para pizzarias e lanchonetes que saem da comanda de papel: pedidos pelo
QR Code ou lançados pela equipe, fila da cozinha, PDV com controle de caixa,
editor de cardápio e relatórios. Todos os dados da "Forno Demo" são fictícios.

O pagamento é feito no caixa, do modo convencional: o operador só registra a
forma recebida. Não há integração com maquininha, Pix ou banco.

## Como rodar

Requer Node 24 ou superior.

```sh
npm install
npm run dev      # API em :3000 e interface em :5173
```

Abra <http://localhost:5173/painel/forno-demo>. Usuários de demonstração, ambos
com a senha `demo1234`:

| Usuário | Perfil | Acesso |
|---|---|---|
| `admin` | Administrador | Tudo, incluindo cardápio, relatórios, equipe e estornos |
| `caixa` | Equipe | Pedidos, novo pedido, caixa, mesas e disponibilidade |

Para testar em um celular na mesma rede, abra o painel pelo IP do computador,
assim o QR Code aponta para um endereço acessível.

| Comando | Função |
|---|---|
| `npm test` | Testes do servidor |
| `npm run typecheck` | Verificação de tipos |
| `npm run build && npm start` | Versão compilada em :3000 |

## Fluxo de trabalho

1. **Abrir o caixa** com o dinheiro inicial da gaveta.
2. **Receber pedidos** de duas formas: o cliente pelo QR Code com o código da
   mesa, ou a equipe em **Novo pedido**, para mesa ou balcão.
3. **Preparar:** o pedido do QR Code entra em **Novos** e aguarda aceite. O da
   equipe já entra aceito. O preço é sempre calculado no servidor.
4. **Receber:** em **Caixa**, escolha a conta, ajuste serviço ou desconto e
   registre um ou mais pagamentos. Dinheiro calcula o troco.
5. **Fechar a conta** quando o saldo zerar. Uma mesa precisa ter os pedidos
   concluídos; no balcão, o cliente pode pagar antes de a pizza ficar pronta.
6. **Fechar o caixa** com o valor contado. O sistema mostra a diferença.

## Cardápio

Em **Cardápio**, o administrador cria categorias com horário opcional, produtos,
sabores com preço por tamanho, tamanhos, grupos de opções e mesas, além dos dados
da loja, da regra de pizza com vários sabores e da taxa de serviço. Pedidos
guardam cópia do nome e do preço, então editar ou excluir itens não altera o
histórico. Combos são produtos com grupos de escolha obrigatória.

## Configuração

| Variável | Padrão | Uso |
|---|---|---|
| `PORT`, `HOST` | `3000`, `0.0.0.0` | Endereço do servidor |
| `DATA_DIR` | `.data/pg` | Pasta do banco |
| `APP_SECRET` | Só em desenvolvimento | Assina tokens; obrigatória em produção |
| `SEED_PASSWORD` | `demo1234` | Senha dos usuários iniciais; obrigatória em produção |

## Limitações atuais

- **Não emite documento fiscal.** O recibo impresso informa isso. A emissão de
  NFC-e e as regras do estado precisam ser tratadas antes do uso em uma loja real.
- O banco é o PGlite, um Postgres embutido com uma só conexão. Em produção, a
  troca por um Postgres gerenciado exige adaptar `src/server/db.ts`.
- Os dados da demonstração são a única loja. Ainda não há cadastro de novas lojas.
- Não há delivery, cadastro de clientes nem controle de estoque.
- Conta fechada não pode ser reaberta, e estorno só vale com a conta e o caixa abertos.
- Atrás de um proxy, configure `trustProxy` no Fastify. Sem isso, todos os
  clientes parecem vir do mesmo IP nos limites de tentativas.
- A impressão usa a janela do navegador. Ainda não existem modos tablet e totem,
  nem firmware do ESP32.
