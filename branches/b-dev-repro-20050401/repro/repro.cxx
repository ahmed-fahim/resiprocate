#include "resiprocate/MessageFilterRule.hxx"
#include "resiprocate/Security.hxx"
#include "resiprocate/SipStack.hxx"
#include "resiprocate/StackThread.hxx"
#include "resiprocate/dum/DumThread.hxx"
#include "resiprocate/dum/InMemoryRegistrationDatabase.hxx"
#include "resiprocate/os/DnsUtil.hxx"
#include "resiprocate/os/Log.hxx"
#include "resiprocate/os/Logger.hxx"

#include "repro/CommandLineParser.hxx"
#include "repro/Proxy.hxx"
#include "repro/RequestProcessorChain.hxx"
#include "repro/monkeys/RouteProcessor.hxx"
#include "repro/monkeys/AmIResponsible.hxx"
#include "repro/monkeys/DigestAuthenticator.hxx"
#include "repro/monkeys/LocationServer.hxx"
#include "repro/monkeys/ConstantLocationMonkey.hxx"
#include "repro/UserDb.hxx"
#include "repro/Registrar.hxx"
#include "repro/WebAdmin.hxx"
#include "repro/WebAdminThread.hxx"
#include "repro/ReproServerAuthManager.hxx"


#define RESIPROCATE_SUBSYSTEM Subsystem::REPRO

using namespace repro;
using namespace resip;
using namespace std;

int
main(int argc, char** argv)
{
   /* Initialize a stack */
   CommandLineParser args(argc, argv);
   Log::initialize(args.mLogType, args.mLogLevel, argv[0]);

   Security security;
   SipStack stack(&security);
   if (args.mUdpPort)
   {
      stack.addTransport(UDP, args.mUdpPort);
   }
   if (args.mTcpPort)
   {
      stack.addTransport(TCP,args.mTcpPort);
   }
   if (args.mTlsPort)
   {
      stack.addTransport(TLS,args.mTlsPort);
   }
   if (args.mDtlsPort)
   {
      stack.addTransport(DTLS, args.mDtlsPort);
   }

   StackThread stackThread(stack);

   Registrar registrar;
   InMemoryRegistrationDatabase regData;
   MasterProfile profile;

   /* Initialize a proxy */
   RequestProcessorChain requestProcessors;

   if (args.mRequestProcessorChainName=="StaticTest")
   {
     ConstantLocationMonkey* testMonkey = new ConstantLocationMonkey();
     requestProcessors.addProcessor(std::auto_ptr<RequestProcessor>(testMonkey));
   }
   else
   {
     // Either the chainName is default or we don't know about it
     // Use default if we don't recognize the name
     // Should log about it.
     RequestProcessorChain* locators = new RequestProcessorChain();
  
     RouteProcessor* rp = new RouteProcessor;
     locators->addProcessor(std::auto_ptr<RequestProcessor>(rp));
	 
	 AmIResponsible* isme = new AmIResponsible;
	 locators->addProcessor(std::auto_ptr<RequestProcessor>(isme));
	 
	 // [TODO] !rwm! put Gruu monkey here
	 
	 // [TODO] !rwm! put Tel URI monkey here 
  
     // [TODO] !jf! put static route monkey here

     LocationServer* ls = new LocationServer(regData);
     locators->addProcessor(std::auto_ptr<RequestProcessor>(ls));
  
     requestProcessors.addProcessor(auto_ptr<RequestProcessor>(locators));
    
     if (!args.mNoChallenge)
     {
        DigestAuthenticator* da = new DigestAuthenticator();
        //requestProcessors.addProcessor(std::auto_ptr<RequestProcessor>(da)); 
     }
   }
 
   UserDb userDb;

   Proxy proxy(stack, requestProcessors, userDb);
   proxy.addDomain(DnsUtil::getLocalHostName());
   proxy.addDomain(DnsUtil::getLocalHostName(), 5060);
   proxy.addDomain(DnsUtil::getLocalIpAddress());
   proxy.addDomain(DnsUtil::getLocalIpAddress(), 5060);
   for (std::vector<Uri>::const_iterator i=args.mDomains.begin(); 
        i != args.mDomains.end(); ++i)
   {
      //InfoLog (<< "Adding domain " << i->host() << " " << i->port());
      proxy.addDomain(i->host(), i->port());
   }

   
   WebAdmin admin(userDb,regData);
   WebAdminThread adminThread(admin);

   profile.clearSupportedMethods();
   profile.addSupportedMethod(resip::REGISTER);

   DialogUsageManager* dum = 0;
   DumThread* dumThread = 0;
   
   if (!args.mNoRegistrar)
   {   
      /* Initialize a registrar */
      dum = new DialogUsageManager(stack);
      
      dum->setServerRegistrationHandler(&registrar);
      dum->setRegistrationPersistenceManager(&regData);
      dum->setMasterProfile(&profile);
      
      dumThread = new DumThread(*dum);

     auto_ptr<ServerAuthManager> uasAuth( new ReproServerAuthManager(*dum,userDb));
     dum->setServerAuthManager(uasAuth);
     //stack.registerTransactionUser(*dum);

     // Install rules so that the registrar only gets REGISTERs
     resip::MessageFilterRuleList ruleList;
     resip::MessageFilterRule::MethodList methodList;
     methodList.push_back(resip::REGISTER);
     ruleList.push_back(
        MessageFilterRule(resip::MessageFilterRule::SchemeList(),
                          resip::MessageFilterRule::Any,
                          methodList)
     );
     dum->setMessageFilterRuleList(ruleList);

   }
   stack.registerTransactionUser(proxy);
   

   /* Make it all go */
   stackThread.run();
   proxy.run();
   adminThread.run();
   if (dumThread)
   {
      dumThread->run();
   }
   
   proxy.join();
   stackThread.join();
   adminThread.join();
   if (dumThread)
   {
      dumThread->join();
   }
}
