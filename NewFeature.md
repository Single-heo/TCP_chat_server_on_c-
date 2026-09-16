1 - Create the basic objects to tls -> Like set up server ctx, certificate, and private key.
2 - Modify handle_new_connections -> to Add ssl and put then into tls encryptioon
3 - Edit the client struct for tls features and states like, want read/write handshake and ssl objects
