/*
 * Example low-level D-Bus code.
 * Written by Matthew Johnson <dbus@matthew.ath.cx>
 *
 * This code has been released into the Public Domain.
 * You may do whatever you like with it.
 *
 * Subsequent tweaks by Will Ware <wware@alum.mit.edu>
 * Still in the public domain.
 */
#include <dbus/dbus.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int inhibitor_fd = 0;
/**
 * Call a method on a remote object
 */
int take_inhibitor_lock(DBusConnection *conn, const char *what, const char *who, const char *why, const char *mode)
{
   DBusMessage* msg;
   DBusMessageIter args;
   DBusError err;
   DBusPendingCall* pending;

   // initialiset the errors
   dbus_error_init(&err);

   // create a new method call and check for errors
   msg = dbus_message_new_method_call("org.freedesktop.login1", // target for the method call
                                      "/org/freedesktop/login1", // object to call on
                                      "org.freedesktop.login1.Manager", // interface to call on
                                      "Inhibit"); // method name
   if (NULL == msg) {
      fprintf(stderr, "Message Null\n");
      exit(1);
   }

   // append arguments
   dbus_message_iter_init_append(msg, &args);
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &what)) {
      fprintf(stderr, "Out Of Memory!\n");
      exit(1);
   }

   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &who)) {
      fprintf(stderr, "Out Of Memory!\n");
      exit(1);
   }
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &why)) {
      fprintf(stderr, "Out Of Memory!\n");
      exit(1);
   }
   if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &mode)) {
      fprintf(stderr, "Out Of Memory!\n");
      exit(1);
   }
   // send message and get a handle for a reply
   if (!dbus_connection_send_with_reply (conn, msg, &pending, -1)) { // -1 is default timeout
      fprintf(stderr, "Out Of Memory!\n");
      exit(1);
   }
   if (NULL == pending) {
      fprintf(stderr, "Pending Call Null\n");
      exit(1);
   }
   dbus_connection_flush(conn);

   printf("Request Sent\n");

   // free message
   dbus_message_unref(msg);

   // block until we recieve a reply
   dbus_pending_call_block(pending);

   // get the reply message
   msg = dbus_pending_call_steal_reply(pending);
   if (NULL == msg) {
      fprintf(stderr, "Reply Null\n");
      exit(1);
   }
   // free the pending message handle
   dbus_pending_call_unref(pending);

   // read the parameters
   if (!dbus_message_iter_init(msg, &args))
      fprintf(stderr, "Message has no arguments!\n");
   else
      dbus_message_iter_get_basic(&args, &inhibitor_fd);

   printf("Got Reply: %d \n", inhibitor_fd);

   // free reply
   dbus_message_unref(msg);
   return inhibitor_fd;
}

void drop_inhibitor_lock(int inhibitor_fd) {
   printf("dropped for lock\n");
   close(inhibitor_fd);
}

/**
 * Listens for signals on the bus
 */

static DBusHandlerResult handle_messages(DBusConnection *connection, DBusMessage *message, void *user_data) {
    const char *interface_name = dbus_message_get_interface(message);
    const char *member_name = dbus_message_get_member(message);
    DBusMessageIter args;
    bool sigvalue;

    printf("Got Message\n%s\n%s\n", interface_name, member_name);
    if (dbus_message_is_signal(message, "org.freedesktop.login1.Manager", "PrepareForSleep")) {

       // read the parameters
       if (!dbus_message_iter_init(message, &args))
          fprintf(stderr, "Message Has No Parameters\n");
       else if (DBUS_TYPE_BOOLEAN != dbus_message_iter_get_arg_type(&args))
          fprintf(stderr, "Argument is not bool!\n");
       else
          dbus_message_iter_get_basic(&args, &sigvalue);

       printf("Got Signal with value %d\n", sigvalue);
       if(sigvalue) {
          printf("%s: Line : %d Receive suspend signal fd is %d\n",__func__,__LINE__,inhibitor_fd);
          if (inhibitor_fd > 0)
             drop_inhibitor_lock(inhibitor_fd);
          else
             printf("Error: %s: Line : %d inhibitor lock is already released\n",__func__,__LINE__);
          inhibitor_fd = -1;
       }
       else {
          if (inhibitor_fd == -1)
             inhibitor_fd = take_inhibitor_lock(connection,"sleep", "client2", "Test", "delay");
          else
             printf("Error: %s: Line : %d inhibitor lock is already taken\n",__func__,__LINE__);
          printf("%s: Line : %d suspend fail or resume, reown lock\n",__func__,__LINE__);
       }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void monitor()
{
   DBusMessageIter args;
   DBusConnection* conn;
   DBusError err;
   int ret;
   bool sigvalue;

   // initialise the errors
   dbus_error_init(&err);

   // connect to the bus and check for errors
   conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
   if (dbus_error_is_set(&err)) {
      fprintf(stderr, "Connection Error (%s)\n", err.message);
      dbus_error_free(&err);
   }
   if (NULL == conn) {
      exit(1);
   }

   inhibitor_fd = take_inhibitor_lock(conn, "sleep", "client", "suspendcallback", "delay");

   dbus_connection_add_filter(conn, handle_messages, NULL, NULL);
   // add a rule for which messages we want to see
   dbus_bus_add_match(conn, "type='signal',path='/org/freedesktop/login1',interface='org.freedesktop.login1.Manager',member='PrepareForSleep'", &err); // see signals from the given interface
   dbus_connection_flush(conn);
   if (dbus_error_is_set(&err)) {
      fprintf(stderr, "Match Error (%s)\n", err.message);
      exit(1);
   }
   printf("Match rule sent\n");


   // non blocking read of the next available message
   // while 
   while (1) {
      dbus_connection_read_write_dispatch(conn, -1);
   }
}

int main(int argc, char** argv)
{
   monitor();
   return 0;
}
