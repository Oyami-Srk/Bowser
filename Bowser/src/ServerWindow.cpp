
#include <NetEndpoint.h>
#include <NetAddress.h>

#include <UTF8.h>
#include <Path.h>
#include <FilePanel.h>
#include <MenuItem.h>
#include <Autolock.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "Bowser.h"
#include "IRCView.h"
#include "StringManip.h"
#include "ChannelWindow.h"
#include "DCCConnect.h"
#include "MessageWindow.h"
#include "ListWindow.h"
#include "IgnoreWindow.h"
#include "NotifyWindow.h"
#include "StatusView.h"
#include "ServerWindow.h"

int32 ServerWindow::ServerSeed			= 0;
BLocker ServerWindow::identLock;

ServerWindow::ServerWindow (
	const char *id_,

	BList *nicks,
	const char *port,
	const char *name,
	const char *ident,
	const char **events_,
	bool motd_,
	bool identd_,
	const char *cmds_)

	: ClientWindow (
		id_,
		ServerSeed++,
		id_,
		(const char *)nicks->FirstItem(),
		BRect (105,105,550,400)),

		lnicks (nicks),
		lport (port),
		lname (name),
		lident (ident),
		nickAttempt (0),
		myNick ((const char *)nicks->FirstItem()),
		myLag ("0.000"),
		isConnected (false),
		isConnecting (true),
		reconnecting (false),
		isQuitting (false),
		checkingLag (false),
		retry (0),
		retryLimit (25),
		lagCheck (0),
		lagCount (0),
		endPoint (0),
		send_buffer (0),
		send_size (0),
		parse_buffer (0),
		parse_size (0),
		events (events_),
		motd (motd_),
		initialMotd (true),
		identd (identd_),
		hostnameLookup (false),
		cmds (cmds_),
		localAddress (""),
		localIP ("")
{
	SetSizeLimits (200,2000,150,2000);

	SetTitle ("Bowser: Connecting");

	myFont = *(bowser_app->GetClientFont (F_SERVER));
	ctcpReqColor = bowser_app->GetColor (C_CTCP_REQ);
	ctcpRpyColor = bowser_app->GetColor (C_CTCP_RPY);
	whoisColor   = bowser_app->GetColor (C_WHOIS);
	errorColor   = bowser_app->GetColor (C_ERROR);
	quitColor    = bowser_app->GetColor (C_QUIT);
	joinColor    = bowser_app->GetColor (C_JOIN);
	noticeColor  = bowser_app->GetColor (C_NOTICE);
	textColor    = bowser_app->GetColor (C_SERVER);
	
	AddShortcut('W', B_COMMAND_KEY, new BMessage(M_SERVER_ALTW)); 

	status->AddItem (new StatusItem (
		serverName.String(), 0),
		true);
		
	status->AddItem (new StatusItem (
		"Lag: ",
		0,
		STATUS_ALIGN_LEFT),
		true);
	status->SetItemValue (STATUS_LAG, myLag.String());

	status->AddItem (new StatusItem (
		0,
		0,
		STATUS_ALIGN_LEFT),
		true);
	status->SetItemValue (STATUS_NICK, myNick.String());
	
	SetPulseRate (0);

	// We pack it all up and ship it off to the
	// the establish thread.  Establish can
	// work in getting connecting regardless of
	// what this instance of ServerWindow is doing
/*	BMessage *msg (new BMessage);
	msg->AddString ("id", id.String());
	msg->AddString ("port", lport.String());
	msg->AddString ("ident", lident.String());
	msg->AddString ("name", lname.String());
	msg->AddString ("nick", myNick.String());
	msg->AddBool ("identd", identd);
	msg->AddPointer ("server", this);
*/
	loginThread = spawn_thread (
		Establish,
		"complimentary_tote_bag",
		B_NORMAL_PRIORITY,
		new BMessenger(this));
	
	resume_thread (loginThread);

	BMessage aMsg (M_SERVER_STARTUP);
	aMsg.AddString ("server", serverName.String());
	bowser_app->PostMessage (&aMsg);
}

ServerWindow::~ServerWindow (void)
{
	if (send_buffer)  delete [] send_buffer;
	if (parse_buffer) delete [] parse_buffer;

	char *nick;
	#ifdef __x86_64__
	while ((nick = (char *)lnicks->RemoveItem (0)) != 0)
	#else
	while ((nick = (char *)lnicks->RemoveItem (0L)) != 0)
	#endif
		delete [] nick;
	delete lnicks;

	for (int32 i = 0; i < MAX_EVENTS; ++i)
		delete [] events[i];
	delete [] events;
}


bool
ServerWindow::QuitRequested()
{
	//printf ("ServerWindow::QuitRequested, %s\n", id.String());
	BMessage *msg (CurrentMessage());
	bool shutdown (false);

	if (msg->HasBool ("bowser:shutdown"))
		msg->FindBool ("bowser:shutdown", &shutdown);

//	if (isConnecting && !hasWarned && !shutdown)
//	{
//		Display ("* To avoid network instability, it is HIGHLY recommended\n", 0);
//		Display ("* that you have finished the connection process before\n", 0);
//		Display ("* closing Bowser. If you _still_ want to try to quit,\n", 0);
//		Display ("* click the close button again.\n", 0);
//
//		hasWarned = true;
//		return false;
//	}

	if (msg->HasString ("bowser:quit"))
	{
		const char *quitstr;

		msg->FindString ("bowser:quit", &quitstr);
		quitMsg = quitstr;
	}

	bool sentDemise (false);

	for (int32 i = 0; i < clients.CountItems(); ++i)
	{
		ClientWindow *client ((ClientWindow *)clients.ItemAt (i));

		if (client != this)
		{
			BMessage msg (B_QUIT_REQUESTED);

			msg.AddBool ("bowser:part", false);
			client->PostMessage (&msg);

			sentDemise = true;
		}
	}

	isQuitting = true;
	if (sentDemise)
		return false;

	if (isConnected && endPoint)
	{
		if (quitMsg.Length() == 0)
		{
			const char *expansions[1];
			BString version (bowser_app->BowserVersion());
			expansions[0] = version.String();
			quitMsg << "QUIT :" << ExpandKeyed (bowser_app->GetCommand (CMD_QUIT).String(), "V", expansions);
		}

		SendData (quitMsg.String());
	}

	// Don't kill login thread.. it will figure
	// things out for itself
	
	// Tell the app about our death, he may care
	BMessage aMsg (M_SERVER_SHUTDOWN);
	aMsg.AddString ("server", serverName.String());
	bowser_app->PostMessage (&aMsg);
	
	if (bowser_app->GetShowSetupState())
	{
		bowser_app->PostMessage (M_SETUP_ACTIVATE);
	}
		
	return ClientWindow::QuitRequested();

}

void
ServerWindow::MessageReceived (BMessage *msg)
{
	switch (msg->what)
	{
		case M_PARSE_LINE:
		{
			const char *buffer;

			msg->FindString ("line", &buffer);
			ParseLine (buffer);

			break;
		}
		
		case M_GET_ESTABLISH_DATA:
		{
			BMessage reply (B_REPLY);
			reply.AddString ("id", id.String());
			reply.AddString ("port", lport.String());
			reply.AddString ("ident", lident.String());
			reply.AddString ("name", lname.String());
			reply.AddString ("nick", myNick.String());
			reply.AddBool ("identd", identd);
			reply.AddBool ("reconnecting", reconnecting);
			reply.AddPointer("server", this);
			msg->SendReply(&reply);
			break;
		}

		// Client's telling us it closed
		case M_CLIENT_SHUTDOWN:
		{
			ClientWindow *client;

			msg->FindPointer ("client", reinterpret_cast<void **>(&client));
			clients.RemoveItem (client);

			if (isQuitting && clients.CountItems() <= 1)
				PostMessage (B_QUIT_REQUESTED);

			break;
		}

		case M_SERVER_SEND:
		{
			BString buffer;
			int32 i;

			for (i = 0; msg->HasString ("data", i); ++i)
			{
				const char *str;

				msg->FindString ("data", i, &str);
				buffer << str;
			}

			SendData (buffer.String());

			break;
		}
		
		case M_SLASH_RECONNECT:
		{
		
			if (!isConnected && !isConnecting)
			{
				PostMessage (M_SERVER_DISCONNECT);
			}
			
			break;
		
		}
		
		case M_SERVER_DISCONNECT:
		{
			// update lag meter
			SetPulseRate (0);
			myLag = "CONNECTION PROBLEM";
			PostMessage (M_LAG_CHANGED);
			checkingLag = false;
		
			// let the user know
			if (isConnected)
			{
				BString tempString;
				tempString << "[@] Disconnected from " << serverName << "\n";
				Display (tempString.String(), &errorColor);
				DisplayAll (tempString.String(), false, &errorColor, &serverFont);
			}
			
			isConnected = false;
			isConnecting = false;		
			
						
			// setup reconnect
			HandleReconnect();
			
			break;
			
		}
		
		case M_REJOIN_ALL:
		{	
			bool channelOnly (true);
			for (int32 i = 0; i < clients.CountItems(); ++i)
			{
				ClientWindow *client ((ClientWindow *)clients.ItemAt (i));
	
				if (!channelOnly || dynamic_cast<ChannelWindow *>(client))
				{
					BMessage msg (M_REJOIN);
					msg.AddString ("nickname", myNick.String());
					client->PostMessage (&msg);
				}
			}
			break;
		}
		
		case M_LAG_CHANGED:
		{
			status->SetItemValue(STATUS_LAG, myLag.String());
			BMessage newmsg(M_LAG_CHANGED);
			newmsg.AddString("lag", myLag);
			Broadcast(&newmsg);
			break;	
		}

		case CYCLE_WINDOWS:
		{
			ClientWindow *client;
			bool found (false);

			msg->FindPointer ("client", reinterpret_cast<void **>(&client));

			for (int32 i = 0; i < clients.CountItems(); ++i)
				if (client == (ClientWindow *)clients.ItemAt (i)
				&&  i != clients.CountItems() - 1)
				{
					found = true;
					client = (ClientWindow *)clients.ItemAt (i + 1);
					break;
				}

			if (!found)
				client = (ClientWindow *)clients.FirstItem();
					
			client->Activate();
			break;
		}

		case CYCLE_BACK:
		{
			ClientWindow *client, *last (0);

			msg->FindPointer ("client", reinterpret_cast<void **>(&client));
			
			for (int32 i = 0; i < clients.CountItems(); ++i)
			{
				if (client == (ClientWindow *)clients.ItemAt (i))
					break;

				last = (ClientWindow *)clients.ItemAt (i);
			}

			if (!last)
				last = (ClientWindow *)clients.LastItem();
			last->Activate();
			break;
		}

		case OPEN_MWINDOW:
		{
			ClientWindow *client;
			const char *theNick;

			msg->FindString ("nick", &theNick);

			if (!(client = Client (theNick)))
			{
				client = new MessageWindow (
					theNick,
					sid,
					serverName.String(),
					sMsgr,
					myNick.String(),
					"");

				clients.AddItem (client);
				client->Show();
			}
			else
				client->Activate (true);

			if (msg->HasMessage ("msg"))
			{
				BMessage buffer;

				msg->FindMessage ("msg", &buffer);
				client->PostMessage (&buffer);
			}
			
			break;
		}

		case DCC_ACCEPT:
		{
			bool cont (false);
			const char *nick,
				*size,
				*ip,
				*port;
			BPath path;
			
			msg->FindString("bowser:nick", &nick);
			msg->FindString("bowser:size", &size);
			msg->FindString("bowser:ip", &ip);
			msg->FindString("bowser:port", &port);
			
			if (msg->HasString ("path"))
				path.SetTo (msg->FindString ("path"));
			else
			{
				const char *file;
				entry_ref ref;

				msg->FindRef ("directory", &ref);
				msg->FindString("name", &file);

				BDirectory dir (&ref);
				path.SetTo (&dir, file);
			}

			if (msg->HasBool ("continue"))
				msg->FindBool ("continue", &cont);

			DCCReceive *view;
			view = new DCCReceive (
				nick,
				path.Path(),
				size,
				ip,
				port,
				cont);
			
			BMessage aMsg (M_DCC_FILE_WIN);
			aMsg.AddPointer ("view", view);
			be_app->PostMessage (&aMsg);
			break;
		}


		case B_CANCEL:

			if (msg->HasPointer ("source"))
			{
				BFilePanel *fPanel;

				msg->FindPointer ("source", reinterpret_cast<void **>(&fPanel));
				delete fPanel;
			}
			break;

		case CHAT_ACCEPT:
		{
			int32 acceptDeny;
			const char *theNick, *theIP, *thePort;
			msg->FindInt32("which", &acceptDeny);
			if(acceptDeny)
				return;
			msg->FindString("nick", &theNick);
			msg->FindString("ip", &theIP);
			msg->FindString("port", &thePort);


			MessageWindow *window (new MessageWindow (
				theNick,
				sid,
				serverName.String(),
				sMsgr,
				myNick.String(),
				"",
				true,
				false,
				theIP,
				thePort));
			clients.AddItem (window);
			window->Show();
			break;	
		}

		case CHAT_ACTION: // DCC chat
		{
			// ToDo: Update to use hostAddress
			ClientWindow *client;
			const char *theNick;
			BString theId;

			msg->FindString ("nick", &theNick);
			theId << theNick << " [DCC]";

			if ((client = Client (theId.String())) == 0)
			{
				client = new MessageWindow (
					theNick,
					sid,
					serverName.String(),
					sMsgr,
					theId.String(),
					"",
					true,
					true);

				clients.AddItem (client);
				client->Show();
			}

			break;
		}

		case M_SERVER_ALTW:
		{
		
			if (bowser_app->GetAltwServerState())
			{
				sMsgr = BMessenger (this);
				BMessage msg (B_QUIT_REQUESTED);
				msg.AddString ("bowser:quit", "");
				sMsgr.SendMessage (&msg);
			}
			
			break;
		}

		case CHOSE_FILE: // DCC send
		{
			const char *nick;
			entry_ref ref;
			off_t size;
			msg->FindString ("nick", &nick);
			msg->FindRef ("refs", &ref); // get file
			
			BEntry entry (&ref);
			BPath path (&entry);
			// PRINT(("file path: %s\n", path.Path()));
			entry.GetSize (&size);

			BString ssize;
			ssize << size;

			// because of a bug in the be library
			// we have to get the sockname on this
			// socket, and not the one that DCCSend
			// binds.  calling getsockname on a
			// a binded socket will return the
			// LAN ip over the DUN one 
			
			hostent *hp = gethostbyname (localAddress.String());
				
			DCCSend *view;
			view = new DCCSend (
				nick,
				path.Path(),
				ssize.String(),
				sMsgr,
				*((struct in_addr*)(hp->h_addr_list)[0]));
			BMessage msg (M_DCC_FILE_WIN);
			msg.AddPointer ("view", view);
			be_app->PostMessage (&msg);
			break;
		}
		
		case M_ADD_RESUME_DATA:
		{
			AddResumeData (msg);
			break;
		}

		default:
			ClientWindow::MessageReceived (msg);
	}
}

void
ServerWindow::MenusBeginning (void)
{
	mIgnore->SetEnabled (isConnected);
	mList->SetEnabled (isConnected);
	mNotifyWindow->SetEnabled (isConnected);

	ClientWindow::MenusBeginning();
}

void
ServerWindow::DispatchMessage (BMessage *msg, BHandler *handler)
{
	if (msg->what == B_PULSE)
		Pulse();

	BWindow::DispatchMessage (msg, handler);
}

void
ServerWindow::Pulse (void)
{
	if (isConnected)
	{
		if (!checkingLag)
		{
			lagCheck = system_time();
			lagCount = 1;
			checkingLag = true;
			BMessage send (M_SERVER_SEND);
			AddSend (&send, "BOWSER_LAG_CHECK");
			AddSend (&send, endl);
		}
		else
		{
			if (lagCount > 4)
			{
				// connection problems?
				myLag = "CONNECTION PROBLEM";
				BMessage msg(M_LAG_CHANGED);
				PostMessage(&msg);
			}
			else
			{
				// wait some more
				char lag[15] = "";
				sprintf (lag, "%ld0.000+", lagCount);
				myLag = lag;
				++lagCount;
				BMessage msg(M_LAG_CHANGED);
				PostMessage(&msg);
			}
		}	
	}
}



///////////////////////////////////////////////////////////////////////////
///////////////////////////// PROGRAM FUNCTIONS ///////////////////////////
///////////////////////////////////////////////////////////////////////////

int32
ServerWindow::Establish (void *arg)
{
	
	BMessenger *sMsgr (reinterpret_cast<BMessenger *>(arg));
	const char *id, *port, *ident, *name, *nick;
	ServerWindow *server;
	bool identd, reconnecting (false);
	thread_id establishThread = find_thread(NULL);
	BMessage msg;
	if (sMsgr->IsValid())
		sMsgr->SendMessage(M_GET_ESTABLISH_DATA, &msg);	
	msg.FindString ("id", &id);
	msg.FindString ("port", &port);
	msg.FindString ("ident", &ident);
	msg.FindString ("name", &name);
	msg.FindString ("nick", &nick);
	msg.FindBool ("identd", &identd);
	msg.FindBool ("reconnecting", &reconnecting);
	msg.FindPointer ("server", reinterpret_cast<void **>(&server));

	if (reconnecting)
	{
		if (server->retry > 0) {
			snooze (2000000); // wait 2 seconds
		}
		server->retry++;
		BMessage statusMsgR (M_DISPLAY);
		BString tempStringR;
		tempStringR << "[@] Attempting to reconnect (Retry " << server->retry << " of " << server->retryLimit << ")\n";
		server->PackDisplay (&statusMsgR, tempStringR.String(), &(server->errorColor));
		server->PostMessage(&statusMsgR);
		server->DisplayAll (tempStringR.String(), false, &(server->errorColor), &(server->serverFont));
	}
		
	
	BMessage statusMsg0 (M_DISPLAY);
	BString tempString0;
	tempString0 << "[@] Attempting a connection to " << id << ":" << port << "...\n";
	
	server->PackDisplay (&statusMsg0, tempString0.String(), &(server->errorColor));
	server->PostMessage(&statusMsg0);
	
	BNetAddress address;

	if (address.SetTo (id, atoi (port)) != B_NO_ERROR)
	{
		BMessage statusMsgI (M_DISPLAY);
		server->PackDisplay (&statusMsgI, "[@] The address and port seem to be invalid. Make sure your Internet connection is operational.\n", &(server->errorColor));
		server->PostMessage (&statusMsgI);

		server->PostMessage (M_SERVER_DISCONNECT);
		delete sMsgr;
		return B_ERROR;
	}
	BNetEndpoint *endPoint (new BNetEndpoint);
	
	if (!endPoint || endPoint->InitCheck() != B_NO_ERROR)
	{
		if (server->Lock())
		{
			BMessage statusMsgC (M_DISPLAY);
			server->PackDisplay (&statusMsgC, "[@] Could not create connection to address and port. Make sure your Internet connection is operational.\n", &(server->errorColor));
			server->PostMessage (&statusMsgC);
			server->isConnecting = false;
			server->Unlock();
		}

		if (endPoint)
		{
			endPoint->Close();
			delete endPoint;
			endPoint = 0;
		}
		delete sMsgr;
		return B_ERROR;
	}

	// just see if he's still hanging around before
	// we got blocked for a minute
	if (!sMsgr->IsValid())
	{
		endPoint->Close();
		delete endPoint;
		endPoint = 0;
		delete sMsgr;
		return B_ERROR;
	}

	if(bowser_app->GetHideSetupState())
	{
		bowser_app->PostMessage (M_SETUP_HIDE);
	}
	
	BMessage statusMsgO (M_DISPLAY);
	server->PackDisplay (&statusMsgO, "[@] Connection open, waiting for reply from server\n", &(server->errorColor));
	server->PostMessage(&statusMsgO);

	server->myLag = "0.000";
	server->PostMessage (M_LAG_CHANGED);

	identLock.Lock();
	if (endPoint->Connect (address) == B_NO_ERROR)
	{
		struct sockaddr_in sin;
		socklen_t namelen (sizeof (struct sockaddr_in));
		BMessage statusMsg1 (M_DISPLAY);
		server->PackDisplay (&statusMsg1, "[@] Established\n", &(server->errorColor));
		server->PostMessage(&statusMsg1);
		// Here we save off the local address for DCC and stuff
		// (Need to make sure that the address we use to connect
		//  is the one that we use to accept on)
		server->Lock();
		getsockname (endPoint->Socket(), (struct sockaddr *)&sin, &namelen);
		server->localuIP = sin.sin_addr.s_addr;
		server->Unlock();
				
		if (identd)
		{
			BMessage statusMsg2 (M_DISPLAY);
			server->PackDisplay (&statusMsg2, "[@] Spawning Ident daemon (10 sec timeout)\n", &(server->errorColor));
			server->PostMessage(&statusMsg2);
			BNetEndpoint identPoint, *accepted;
			BNetAddress identAddress (sin.sin_addr, 113);
			BNetBuffer buffer;
			char received[64];

			if (sMsgr->IsValid()
			&&  identPoint.InitCheck()             == B_OK
			&&  identPoint.Bind (identAddress)     == B_OK
			&&  identPoint.Listen()                == B_OK
			&& (accepted = identPoint.Accept(10000))    != 0   // 10 sec timeout
			&&  accepted->Receive (buffer, 64)     >= 0
			&&  buffer.RemoveString (received, 64) == B_OK)
			{
				int32 len;
	
				received[63] = 0;
				while ((len = strlen (received))
				&&     isspace (received[len - 1]))
					received[len - 1] = 0;

				BNetBuffer output;
				BString string;

				string.Append (received);
				string.Append (" : USERID : BeOS : ");
				string.Append (ident);
				string.Append ("\r\n");

				output.AppendString (string.String());
				accepted->Send (output);
				delete accepted;
				
				BMessage statusMsg3 (M_DISPLAY);
				server->PackDisplay (&statusMsg3, "[@] Replied to Ident request\n", &(server->errorColor));
				server->PostMessage(&statusMsg3);
			}
			
			BMessage statusMsgD (M_DISPLAY);
			server->PackDisplay (&statusMsgD, "[@] Deactivated daemon\n", &(server->errorColor));
			server->PostMessage(&statusMsgD);
		}
		
		BMessage statusMsg4 (M_DISPLAY);
		server->PackDisplay (&statusMsg4, "[@] Handshaking\n", &(server->errorColor));
		server->PostMessage(&statusMsg4);
		BString string;

		string = "USER ";
		string.Append (ident);
		string.Append (" localhost ");
		string.Append (id);
		string.Append (" :");
		string.Append (name);


		if (sMsgr->LockTarget())
		{
			server->endPoint = endPoint;
			server->SendData (string.String());

			string = "NICK ";
			string.Append (nick);
			server->SendData (string.String());
			server->Unlock();
		}
		identLock.Unlock();
	}

	else // No endpoint->connect
	{
		identLock.Unlock();

		BMessage statusMsgE (M_DISPLAY);
		server->PackDisplay (&statusMsgE, "[@] Could not establish a connection to the server. Sorry.\n", &(server->errorColor));
		server->PostMessage (&statusMsgE);
		server->PostMessage (M_SERVER_DISCONNECT);
		endPoint->Close();
		delete endPoint;
		endPoint = 0;
		delete sMsgr;		
		return B_ERROR;
	}
		
	
	// Don't need this anymore
	struct fd_set eset, rset, wset;
	struct timeval tv = {0, 0};

	FD_ZERO (&eset);
	FD_ZERO (&rset);
	FD_ZERO (&wset);

	BString buffer;

	while (sMsgr->IsValid() 
		&& (establishThread == server->loginThread)
		&& sMsgr->LockTarget())
	{
		BNetBuffer input (1024);
		int32 length (0);

		FD_SET (endPoint->Socket(), &eset);
		FD_SET (endPoint->Socket(), &rset);
		FD_SET (endPoint->Socket(), &wset);
		if (select (endPoint->Socket() + 1, &rset, 0, &eset, &tv) > 0
		&&  FD_ISSET (endPoint->Socket(), &rset))
		{	
			if ((length = endPoint->Receive (input, 1024)) > 0)
			{
				BString temp;
				int32 index;
	
				temp.SetTo ((char *)input.Data(), input.Size());
				buffer += temp;
	
				while ((index = buffer.FindFirst ('\n')) != B_ERROR)
				{
					temp.SetTo (buffer, index);
					buffer.Remove (0, index + 1);
	
					temp.RemoveLast ("\r");
		
					#ifdef DEV_BUILD
	
					if (DumpReceived)
					{
						printf ("RECEIVED: (%ld) \"", temp.Length());
						for (int32 i = 0; i < temp.Length(); ++i)
						{
							if (isprint (temp[i]))
								printf ("%c", temp[i]);
							else
								printf ("[0x%02x]", temp[i]);
						}
						printf ("\"\n");
					}
	
					#endif
		
					// We ship it off this way because
					// we want this thread to loop relatively
					// quickly.  Let ServerWindow's main thread
					// handle the processing of incoming data!
					BMessage msg (M_PARSE_LINE);
					msg.AddString ("line", temp.String());
					server->PostMessage (&msg);
				}
			}
			if (FD_ISSET (endPoint->Socket(), &eset)
			|| (FD_ISSET (endPoint->Socket(), &rset) && length == 0)
			|| !FD_ISSET (endPoint->Socket(), &wset)
			|| length < 0)
			{
				// we got disconnected :(
				
				// print interesting info
				/* PRINT(("Negative from endpoint receive! (%ld)\n", length));
				PRINT(("(%d) %s\n",
					endPoint->Error(),
					endPoint->ErrorStr()));

				PRINT(("eset : %s\nrset: %s\nwset: %s\n",
					FD_ISSET (endPoint->Socket(), &eset) ? "true" : "false",
					FD_ISSET (endPoint->Socket(), &rset) ? "true" : "false",
					FD_ISSET (endPoint->Socket(), &wset) ? "true" : "false"));
				*/		

				// tell the user all about it
				server->PostMessage (M_SERVER_DISCONNECT);
				server->Unlock();
				break;
			}
		}
		// take a nap, so the ServerWindow can do things
		server->Unlock();
		snooze (20000);
	}
	endPoint->Close();
	delete endPoint;
	endPoint = 0;
	
	delete sMsgr;
	return B_OK;
}

void
ServerWindow::SendData (const char *cData)
{
	int32 length;
	BString data (cData);

	data.Append("\r\n");
	length = data.Length() + 1;

	// The most it could be is that every
	// stinking character is a utf8 character.
	// Which can be at most 3 bytes.  Hence
	// our multiplier of 3

	if (send_size < length * 3UL)
	{
		if (send_buffer) delete [] send_buffer;
		send_buffer = new char [length * 3];
		send_size = length * 3;
	}

	int32 dest_length (send_size), state (0);

	convert_from_utf8 (
		B_ISO1_CONVERSION,
		data.String(), 
		&length,
		send_buffer,
		&dest_length,
		&state);

	if ((endPoint != 0 && (length = endPoint->Send (send_buffer, strlen (send_buffer))) < 0)
		|| endPoint == 0)
	{
		// doh, we aren't even connected.
		
		if (!reconnecting && !isConnecting) {
			PostMessage (M_SERVER_DISCONNECT);
		}
		
		
	}
	
	#ifdef DEV_BUILD
	data.RemoveAll ("\n");
	data.RemoveAll ("\r");
	if (DumpSent) printf("SENT: (%ld) \"%s\"\n", length, data.String());
	#endif
}

void
ServerWindow::ParseLine (const char *cData)
{
	BString data (FilterCrap (cData));

	int32 length (data.Length() + 1);

	if (parse_size < length * 3UL)
	{
		if (parse_buffer) delete [] parse_buffer;
		parse_buffer = new char [length * 3];
		parse_size = length * 3;
	}

	int32 dest_length (parse_size), state (0);

	convert_to_utf8 (
		B_ISO1_CONVERSION,
		data.String(), 
		&length,
		parse_buffer,
		&dest_length,
		&state);

	if (ParseEvents (parse_buffer))
		return;

	data.Append("\n");
	Display (data.String(), 0);
}

ClientWindow *
ServerWindow::Client (const char *cName)
{
	ClientWindow *client (0);

	for (int32 i = 0; i < clients.CountItems(); ++i)
	{
		ClientWindow *item ((ClientWindow *)clients.ItemAt (i));

		if (strcasecmp (cName, item->Id().String()) == 0)
		{
			client = item;
			break;
		}
	}

	return client;
}

ClientWindow *
ServerWindow::ActiveClient (void)
{
	ClientWindow *client (0);

	for (int32 i = 0; i < clients.CountItems(); ++i)
		if (((ClientWindow *)clients.ItemAt (i))->IsActive())
			client = (ClientWindow *)clients.ItemAt (i);

	return client;
}

void
ServerWindow::Broadcast (BMessage *msg)
{
	for (int32 i = 0; i < clients.CountItems(); ++i)
	{
		ClientWindow *client ((ClientWindow *)clients.ItemAt (i));

		if (client != this)
			client->PostMessage (msg);
	}
}

void
ServerWindow::RepliedBroadcast (BMessage *msg)
{
	BMessage cMsg (*msg);
	BAutolock lock (this);

	for (int32 i = 0; i < clients.CountItems(); ++i)
	{
		ClientWindow *client ((ClientWindow *)clients.ItemAt (i));

		if (client != this)
		{
			BMessenger msgr (client);
			BMessage reply;

			msgr.SendMessage (&cMsg, &reply);
		}
	}
}


void
ServerWindow::DisplayAll (
	const char *buffer,
	bool channelOnly,
	const rgb_color *color,
	const BFont *font)
{
	for (int32 i = 0; i < clients.CountItems(); ++i)
	{
		ClientWindow *client ((ClientWindow *)clients.ItemAt (i));

		if (!channelOnly || dynamic_cast<ChannelWindow *>(client))
		{
			BMessage msg (M_DISPLAY);

			PackDisplay (&msg, buffer, color, font);

			client->PostMessage (&msg);
		}
	}

	return;
}

void
ServerWindow::PostActive (BMessage *msg)
{
	BAutolock lock (this);
	ClientWindow *client (ActiveClient());

	if (client)
		client->PostMessage (msg);
	else
		PostMessage (msg);
}


BString
ServerWindow::FilterCrap (const char *data)
{
	BString outData("");
	int32 theChars (strlen (data));

	for (int32 i = 0; i < theChars; ++i)
	{
		if(data[i] > 1 && data[i] < 32)
		{
			// TODO Get these codes working
			if(data[i] == 3)
			{
				#ifdef DEV_BUILD
				if (ViewCodes)
					outData << "[0x03]{";
				#endif

				++i;
				while (i < theChars
				&&   ((data[i] >= '0'
				&&     data[i] <= '9')
				||     data[i] == ','))
				{
					#ifdef DEV_BUILD
					if (ViewCodes)
						outData << data[i];
					#endif

					++i;
				}
				--i;

				#ifdef DEV_BUILD
				if (ViewCodes)
					outData << "}";
				#endif
			}

			#ifdef DEV_BUILD
			else if (ViewCodes)
			{
				char buffer[16];

				sprintf (buffer, "[0x%02x]", data[i]);
				outData << buffer;
			}
			#endif
		}

		else outData << data[i];
	}
	
	return outData;
}

void
ServerWindow::StateChange (BMessage *msg)
{
	// Important to call ClientWindow's State change first
	// We correct some things it sets
	ClientWindow::StateChange (msg);

	if (msg->HasData ("color", B_RGB_COLOR_TYPE))
	{
		const rgb_color *color;
		int32 which;
		ssize_t size;

		msg->FindInt32 ("which", &which);
		msg->FindData (
			"color",
			B_RGB_COLOR_TYPE,
			reinterpret_cast<const void **>(&color),
			&size);

		switch (which)
		{
			case C_CTCP_REQ:
				ctcpReqColor = *color;
				break;

			case C_CTCP_RPY:
				ctcpRpyColor = *color;
				break;

			case C_WHOIS:
				whoisColor = *color;
				break;

			case C_ERROR:
				errorColor = *color;
				break;

			case C_QUIT:
				quitColor = *color;
				break;

			case C_JOIN:
				joinColor = *color;
				break;

			case C_NOTICE:
				noticeColor = *color;
				break;
		}
	}

	if (msg->HasPointer ("font"))
	{
		BFont *font;
		int32 which;

		msg->FindInt32 ("which", &which);
		msg->FindPointer ("font", reinterpret_cast<void **>(&font));

		// ClientWindow may have screwed us, make it right
		if (which == F_TEXT)
			myFont = serverFont;
	}

	if (msg->HasString ("event"))
	{
		const char *event;
		int32 which;

		msg->FindInt32 ("which", &which);
		msg->FindString ("event", &event);

		delete [] events[which];
		events[which] = strcpy (new char [strlen (event) + 1], event);
	}
}

void
ServerWindow::AddResumeData (BMessage *msg)
{
	ResumeData *data;

	data = new ResumeData;

	data->expire = system_time() + 50000000LL;
	data->nick   = msg->FindString ("bowser:nick");
	data->file   = msg->FindString ("bowser:file");
	data->size   = msg->FindString ("bowser:size");
	data->ip     = msg->FindString ("bowser:ip");
	data->port   = msg->FindString ("bowser:port");
	data->path   = msg->FindString ("path");
	data->pos    = msg->FindInt64  ("pos");
	
	//PRINT(("%s %s %s %s %s", data->nick.String(), data->file.String(), 
	//	data->size.String(), data->ip.String(), data->port.String()));
	resumes.AddItem (data);

	BString buffer;

	buffer << "PRIVMSG "
		<< data->nick
		<< " :\1DCC RESUME "
		<< data->file
		<< " "
		<< data->port
		<< " "
		<< data->pos
		<< "\1";

	SendData (buffer.String());
}


uint32
ServerWindow::LocaluIP (void) const
{
	return localuIP;
}


void
ServerWindow::HandleReconnect (void)
{
	if (retry < retryLimit) {
		reconnecting = true;
		isConnecting = true;
		nickAttempt = 0;
		
			
		loginThread = spawn_thread (
			Establish,
			"complimentary_tote_bag",
			B_NORMAL_PRIORITY,
			new BMessenger(this));
	
		resume_thread (loginThread);
	}
	else
	{
		reconnecting = false;
		retry = 0;
		Display ("[@] Giving up. Type /reconnect when you get your act together... or your ISP doesn't stink. Whatever.\n", &errorColor);
	}
}


void
ServerWindow::hostname_askserver (void)
{
	struct sockaddr_in ina;
	ina.sin_addr.s_addr = localuIP;
	
	uint32 a1 (167772160U);
	uint32 a2 (184549375U);
	uint32 b1 (2886729728U);
	uint32 b2 (2887843839U);
	uint32 c1 (3232235520U);
	uint32 c2 (3232301055U);	
	
	if ((localuIP >= a1)  // 10.0.0.0
	||  (localuIP <= a2)  // 10.255.255.255
	||  (localuIP >= b1)  // 172.16.0.0
	||  (localuIP <= b2)  // 172.31.255.255
	||  (localuIP >= c1)  // 192.168.0.0
	||  (localuIP <= c2)) // 192.168.255.255
	{
		// local ip is private
		// ask server what our ip is.
	}
	else
	{
		// we seem to have our own ip... thats good
		
		hostname_resolve(); // resolve localuIP
	}
		
		
	
	printf ("localuIP: %ld\n", localuIP);
	printf ("IP Address : %s\n", localIP.String());

}


void
ServerWindow::hostname_resolve (void)
{

}
