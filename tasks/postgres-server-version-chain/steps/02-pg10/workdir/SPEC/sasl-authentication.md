<!-- source: https://www.postgresql.org/docs/10/sasl-authentication.html -->

PostgreSQL: Documentation: 10: 52.3. SASL Authentication

 | 52.3. SASL Authentication
 | Prev | Up | Chapter 52. Frontend/Backend Protocol | Home | Next

## 52.3. SASL Authentication

52.3.1. SCRAM-SHA-256 authentication
SASL is a framework for authentication in connection-oriented protocols. At the moment, PostgreSQL implements only one SASL authentication mechanism, SCRAM-SHA-256, but more might be added in the future. The below steps illustrate how SASL authentication is performed in general, while the next subsection gives more details on SCRAM-SHA-256.

SASL Authentication Message Flow

To begin a SASL authentication exchange, the server sends an AuthenticationSASL message. It includes a list of SASL authentication mechanisms that the server can accept, in the server's preferred order.

The client selects one of the supported mechanisms from the list, and sends a SASLInitialResponse message to the server. The message includes the name of the selected mechanism, and an optional Initial Client Response, if the selected mechanism uses that.

One or more server-challenge and client-response message will follow. Each server-challenge is sent in an AuthenticationSASLContinue message, followed by a response from client in an SASLResponse message. The particulars of the messages are mechanism specific.

Finally, when the authentication exchange is completed successfully, the server sends an AuthenticationSASLFinal message, followed immediately by an AuthenticationOk message. The AuthenticationSASLFinal contains additional server-to-client data, whose content is particular to the selected authentication mechanism. If the authentication mechanism doesn't use additional data that's sent at completion, the AuthenticationSASLFinal message is not sent.
On error, the server can abort the authentication at any stage, and send an ErrorMessage.

## 52.3.1. SCRAM-SHA-256 authentication
SCRAM-SHA-256 (called just SCRAM from now on) is the only implemented SASL mechanism, at the moment. It is described in detail in RFC 7677 and RFC 5802.
When SCRAM-SHA-256 is used in PostgreSQL, the server will ignore the user name that the client sends in the client-first-message. The user name that was already sent in the startup message is used instead. PostgreSQL supports multiple character encodings, while SCRAM dictates UTF-8 to be used for the user name, so it might be impossible to represent the PostgreSQL user name in UTF-8.
The SCRAM specification dictates that the password is also in UTF-8, and is processed with the SASLprep algorithm. PostgreSQL, however, does not require UTF-8 to be used for the password. When a user's password is set, it is processed with SASLprep as if it was in UTF-8, regardless of the actual encoding used. However, if it is not a legal UTF-8 byte sequence, or it contains UTF-8 byte sequences that are prohibited by the SASLprep algorithm, the raw password will be used without SASLprep processing, instead of throwing an error. This allows the password to be normalized when it is in UTF-8, but still allows a non-UTF-8 password to be used, and doesn't require the system to know which encoding the password is in.
Channel binding has not been implemented yet.

Example

The server sends an AuthenticationSASL message. It includes a list of SASL authentication mechanisms that the server can accept.

The client responds by sending a SASLInitialResponse message, which indicates the chosen mechanism, SCRAM-SHA-256. In the Initial Client response field, the message contains the SCRAM client-first-message.

Server sends an AuthenticationSASLContinue message, with a SCRAM server-first-message as the content.

Client sends a SASLResponse message, with SCRAM client-final-message as the content.

Server sends an AuthenticationSASLFinal message, with the SCRAM server-final-message, followed immediately by an AuthenticationOk message.

 | Prev | Up | Next
 | 52.2. Message Flow  | Home |  52.4. Streaming Replication Protocol
