using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;
using VampireSurvivors.Data;
using VampireSurvivors.Data.Weapons;
using VampireSurvivors.Framework;
using VampireSurvivors.Objects;
using Wilson.UI;
using Character = VampireSurvivors.Objects.Characters.CharacterController;

// Original IL2CPP contracts and real Unity UI objects; not target-game behavior.
public static class UpgradeChoiceFixture
{
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_choice_reset(int managed, uint os);
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_choice_capture(int input);
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_choice_capture_raw(int input);
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_choice_offer_count();
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_choice_offer_has(int child, int weapon);
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern ulong fixture_choice_token();
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_choice_submit(ulong token, int input, int child, int weapon);
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_choice_offthread_submit(ulong token, int input, int child, int weapon);
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_choice_attempted();
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_choice_returned();
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern int fixture_choice_close();
    [DllImport("fixture", CallingConvention=CallingConvention.Cdecl)] static extern IntPtr fixture_choice_reason();

    public static void Run(Action<string,int> check, int managed, uint os, GameManager gm, WSLevelUpPage originalPage)
    {
        WSLevelUpPage page=originalPage;
        GameObject container=null, other=null;
        WSLevelUpItemUI card=null;
        GameSessionData session=null;
        Character first=null,second=null;
        var auxiliary=new List<GameObject>();
        Character Actor(int id, CharacterType kind)
        {
            var actor=new Character {input=new Rewired.Player(id),kind=kind,level=5,hp=100,xp=7};
            actor.ConfigureManagers(new CharacterWeaponsManager(),new CharacterAccessoriesManager());
            return actor;
        }
        void Prepare()
        {
            if(fixture_choice_reset(managed,os)!=0)throw new InvalidOperationException("Choice reset: "+Marshal.PtrToStringUTF8(fixture_choice_reason()));
            if(container)UnityEngine.Object.DestroyImmediate(container);
            foreach(var obj in auxiliary)if(obj)UnityEngine.Object.DestroyImmediate(obj);
            auxiliary.Clear();
            if(!page)page=new GameObject("ChoicePage").AddComponent<WSLevelUpPage>();
            page.gameObject.SetActive(true);page.ChoicesAnimating=false;page.SelectionCount=0;page.LastChoiceOwner=null;
            gm.running=true;gm.stage=new Stage();first=Actor(71,CharacterType.A);second=Actor(93,CharacterType.B);
            gm.players=new List<Character>{first,second};session=new GameSessionData {ActiveCharacter=second};
            page.Configure(session,new LevelUpFactory());
            container=new GameObject("ChoiceContainer",typeof(RectTransform));
            page.ConfigureChoiceGroup(new UIGroup<Transform>(new[]{container.transform}));
            var a=new GameObject("CardA",typeof(RectTransform));a.transform.SetParent(container.transform,false);
            card=a.AddComponent<WSLevelUpItemUI>();card.Configure(WeaponType.FixtureData,new WeaponData(),true);card.ConfigureChoice(page,1);
            var decoration=new GameObject("Decoration");decoration.transform.SetParent(container.transform,false);
            var b=new GameObject("CardB",typeof(RectTransform));b.transform.SetParent(container.transform,false);
            var secondCard=b.AddComponent<WSLevelUpItemUI>();secondCard.Configure(WeaponType.FixtureMissingData,new WeaponData(),true);secondCard.ConfigureChoice(page,1);
        }
        bool Capture() => fixture_choice_capture(93)==0;
        bool Untouched() => page.SelectionCount==0 && first.WeaponsManager.ActiveEquipment.Count==0 && second.WeaponsManager.ActiveEquipment.Count==0;
        bool Refused(int code) => code!=0 && fixture_choice_attempted()==0 && Untouched();
        bool OnlyPositiveOfferRemains(string label)
        {
            // Inspect the captured DTO before submission. Refusal alone hides
            // a regressed filter when selection retains its independent guard.
            var count=fixture_choice_offer_count();
            var positive=fixture_choice_offer_has(2,7002);
            if(count!=1||positive!=1)Debug.Log("CHOICE_OFFER_FILTER "+label+" count="+count+" positive="+positive);
            return count==1 && positive==1;
        }
        int Select(ulong token) => fixture_choice_submit(token,93,0,7001);
        void Case(string name,Func<bool> body)
        {
            var ok=false;
            string detail=null;
            try {Prepare();ok=body();if(!ok)detail=Marshal.PtrToStringUTF8(fixture_choice_reason());}
            catch(Exception error){Debug.Log("CHOICE_FIXTURE "+name+" exception="+error);}
            finally
            {
                var close=fixture_choice_close();
                if(close!=0){ok=false;Debug.Log("CHOICE_FIXTURE close="+Marshal.PtrToStringUTF8(fixture_choice_reason()));}
            }
            if(!ok)Debug.Log("CHOICE_FIXTURE "+name+" detail="+detail);
            check(name,ok?0:1);
        }
        Case("choice_secondary_owner_once",()=> {
            if(!Capture() || !Untouched() || Select(fixture_choice_token())!=0)return false;
            return fixture_choice_attempted()==1 && fixture_choice_returned()==1 && page.SelectionCount==1 &&
                ReferenceEquals(page.LastChoiceOwner,second) && page.LastChoiceType==WeaponType.FixtureData &&
                first.WeaponsManager.ActiveEquipment.Count==0 && second.WeaponsManager.ActiveEquipment.Count==1 &&
                ReferenceEquals(second.WeaponsManager.ActiveEquipment[0].owner,second);
        });
        Case("choice_replay_never_calls_again",()=> {
            if(!Capture())return false;var token=fixture_choice_token();if(Select(token)!=0)return false;
            return Select(token)!=0 && fixture_choice_attempted()==0 && page.SelectionCount==1 && card.ClickCount==1;
        });
        Case("choice_wrong_owner_capture_atomic",()=>fixture_choice_capture(71)!=0 && Untouched());
        Case("choice_wrong_owner_submit",()=>Capture() && Refused(fixture_choice_submit(fixture_choice_token(),71,0,7001)));
        Case("choice_wrong_weapon_submit",()=>Capture() && Refused(fixture_choice_submit(fixture_choice_token(),93,0,7002)));
        Case("choice_decorative_child_refused",()=>Capture() && Refused(fixture_choice_submit(fixture_choice_token(),93,1,7001)));
        Case("choice_out_of_range_refused",()=>Capture() && Refused(fixture_choice_submit(fixture_choice_token(),93,33,7001)));
        Case("choice_zero_token_refused",()=>Capture() && Refused(Select(0)));
        Case("choice_zero_weapon_library_refused",()=> {
            card.Configure(WeaponType.VOID,new WeaponData(),true);
            if(fixture_choice_capture_raw(93)!=0)return false;
            var validOffer=OnlyPositiveOfferRemains("zero");
            var status=fixture_choice_submit(fixture_choice_token(),93,0,0);
            if(status==0||fixture_choice_attempted()!=0)Debug.Log("CHOICE_UNSAFE_TYPE zero callbacks="+page.SelectionCount);
            return Refused(status) && validOffer;
        });
        Case("choice_negative_weapon_library_refused",()=> {
            card.Configure((WeaponType)(-1),new WeaponData(),true);
            if(fixture_choice_capture_raw(93)!=0)return false;
            var validOffer=OnlyPositiveOfferRemains("negative");
            var status=fixture_choice_submit(fixture_choice_token(),93,0,-1);
            if(status==0||fixture_choice_attempted()!=0)Debug.Log("CHOICE_UNSAFE_TYPE negative callbacks="+page.SelectionCount);
            return Refused(status) && validOffer;
        });
        Case("choice_recapture_invalidates_old_token",()=> {
            if(!Capture())return false;var old=fixture_choice_token();
            return Capture() && fixture_choice_token()!=old && Refused(Select(old));
        });
        Case("choice_changed_data_refused",()=> {
            if(!Capture())return false;card.Configure(WeaponType.FixtureData,new WeaponData(),true);return Refused(Select(fixture_choice_token()));
        });
        Case("choice_changed_card_level_refused",()=> {
            if(!Capture())return false;card.ConfigureChoice(page,2);return Refused(Select(fixture_choice_token()));
        });
        Case("choice_limitless_card_refused",()=> {
            if(!Capture())return false;card.ConfigureLimitless(true);return Refused(Select(fixture_choice_token()));
        });
        Case("choice_changed_active_owner_refused",()=> {
            if(!Capture())return false;session.ActiveCharacter=first;return Refused(Select(fixture_choice_token()));
        });
        Case("choice_replaced_actor_same_id_refused",()=> {
            if(!Capture())return false;var replacement=Actor(93,CharacterType.B);gm.players[1]=replacement;session.ActiveCharacter=replacement;
            return Refused(Select(fixture_choice_token())) && replacement.WeaponsManager.ActiveEquipment.Count==0;
        });
        Case("choice_replaced_stage_refused",()=> {
            if(!Capture())return false;gm.stage=new Stage();return Refused(Select(fixture_choice_token()));
        });
        Case("choice_stopped_run_refused",()=> {
            if(!Capture())return false;gm.running=false;return Refused(Select(fixture_choice_token()));
        });
        Case("choice_dead_actor_refused",()=> {
            if(!Capture())return false;second.dead=true;return Refused(Select(fixture_choice_token()));
        });
        Case("choice_disabled_card_refused",()=> {
            if(!Capture())return false;card.interactable=false;return Refused(Select(fixture_choice_token()));
        });
        Case("choice_animating_page_refused",()=> {
            if(!Capture())return false;page.ChoicesAnimating=true;return Refused(Select(fixture_choice_token()));
        });
        Case("choice_destroyed_card_refused",()=> {
            if(!Capture())return false;UnityEngine.Object.DestroyImmediate(card.gameObject);return Refused(Select(fixture_choice_token()));
        });
        Case("choice_wrong_page_association_refused",()=> {
            if(!Capture())return false;other=new GameObject("InactiveOtherPage");other.SetActive(false);auxiliary.Add(other);
            card.ConfigureChoice(other.AddComponent<WSLevelUpPage>(),1);return Refused(Select(fixture_choice_token()));
        });
        Case("choice_wrong_os_thread_refused",()=>Capture() && Refused(fixture_choice_offthread_submit(fixture_choice_token(),93,0,7001)));
        Case("choice_callback_exception_consumes_offer",()=> {
            if(!Capture())return false;var token=fixture_choice_token();card.ThrowAfterClick=true;
            var failed=Select(token)!=0 && fixture_choice_attempted()==1 && fixture_choice_returned()==0 && page.SelectionCount==1;
            return failed && Select(token)!=0 && fixture_choice_attempted()==0 && card.ClickCount==1 && page.SelectionCount==1;
        });
        Case("choice_closed_offer_refused",()=> {
            if(!Capture())return false;var token=fixture_choice_token();return fixture_choice_close()==0 && Refused(Select(token));
        });
        Case("choice_close_after_stopped_run",()=> {
            if(!Capture())return false;gm.running=false;return fixture_choice_close()==0 && Untouched();
        });
        if(container)UnityEngine.Object.DestroyImmediate(container);
        foreach(var obj in auxiliary)if(obj)UnityEngine.Object.DestroyImmediate(obj);
        gm.running=true;
        page.ConfigureChoiceGroup(null);
    }
}
